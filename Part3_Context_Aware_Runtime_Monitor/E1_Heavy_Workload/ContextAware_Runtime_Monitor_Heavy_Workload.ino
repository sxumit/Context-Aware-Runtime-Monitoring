#include <Arduino.h>
#include <math.h>
#include "esp_timer.h"

// =====================================================
// PROJECT 1 - PART 3
// EXPERIMENT 1 - ADAPTIVE RUNTIME MONITOR
// HEAVY WORKLOAD
// =====================================================

#define LED_PIN 4

#define NUM_SAMPLES 1000
#define WORKLOAD_ITERATIONS 10000

#define PERIOD_MS 100
#define DEADLINE_MS 50

#define MONITOR_QUEUE_LENGTH 16

#define TASK_START_EVENT  1
#define TASK_FINISH_EVENT 2


// =====================================================
// ADAPTIVE MONITOR PARAMETERS
// =====================================================

// Number of consecutive sampled deadline violations
// required before reducing monitoring intensity.

#define PRESSURE_THRESHOLD 5

// Number of consecutive sampled healthy executions
// required before increasing monitoring intensity.

#define RECOVERY_THRESHOLD 10


// =====================================================
// MONITORING MODES
// =====================================================

enum MonitorMode {

  FULL_MODE,
  MODERATE_MODE,
  LOW_MODE,
  MINIMAL_MODE
};


// =====================================================
// SAMPLING INTERVAL
// =====================================================

int getSamplingInterval(
  MonitorMode mode
) {

  switch (mode) {

    case FULL_MODE:
      return 1;     // every run

    case MODERATE_MODE:
      return 2;     // every 2nd run

    case LOW_MODE:
      return 5;     // every 5th run

    case MINIMAL_MODE:
      return 10;    // every 10th run
  }

  return 1;
}


// =====================================================
// EVENT
// =====================================================

struct MonitorEvent {

  uint8_t eventType;

  uint16_t sampleNumber;

  uint64_t timestampUs;
};


// =====================================================
// SAMPLE RECORD
// =====================================================

struct SampleRecord {

  unsigned long workloadTimeUs;

  unsigned long monitoredTimeUs;

  unsigned long monitoringOverheadUs;

  bool monitored;

  bool deadlineMiss;

  MonitorMode mode;
};


SampleRecord records[NUM_SAMPLES];


// =====================================================
// QUEUE
// =====================================================

QueueHandle_t monitorQueue;


// =====================================================
// MONITOR STATE
// =====================================================

MonitorMode currentMode = FULL_MODE;

uint64_t startTimestamp = 0;

uint16_t currentSample = 0;

bool taskRunning = false;


// =====================================================
// ADAPTATION STATE
// =====================================================

int pressureCount = 0;

int recoveryCount = 0;


// =====================================================
// STATISTICS
// =====================================================

unsigned long sampledExecutions = 0;

unsigned long deadlineMissesDetected = 0;

unsigned long modeChanges = 0;

unsigned long fullSamples = 0;
unsigned long moderateSamples = 0;
unsigned long lowSamples = 0;
unsigned long minimalSamples = 0;

unsigned long minWorkloadTime = ULONG_MAX;
unsigned long maxWorkloadTime = 0;

double sumWorkloadTime = 0;

double sumWorkloadTimeSquared = 0;

double sumMonitoredTime = 0;

double sumMonitoredTimeSquared = 0;

double sumMonitoringOverhead = 0;


// =====================================================
// MONITOR CPU
// =====================================================

unsigned long long monitorBusyTimeUs = 0;

unsigned long monitorEventsProcessed = 0;


// =====================================================
// RUNTIME MONITOR
// CORE 1
// =====================================================

void runtimeMonitorTask(void *parameter) {

  MonitorEvent event;


  while (true) {

    if (
      xQueueReceive(
        monitorQueue,
        &event,
        portMAX_DELAY
      ) == pdTRUE
    ) {

      unsigned long long monitorStart =
        esp_timer_get_time();


      // =============================================
      // START EVENT
      // =============================================

      if (
        event.eventType ==
        TASK_START_EVENT
      ) {

        startTimestamp =
          event.timestampUs;

        currentSample =
          event.sampleNumber;

        taskRunning = true;
      }


      // =============================================
      // FINISH EVENT
      // =============================================

      else if (
        event.eventType ==
        TASK_FINISH_EVENT
      ) {

        if (!taskRunning) {
          continue;
        }


        uint64_t workloadTimeUs =
          event.timestampUs -
          startTimestamp;


        taskRunning = false;


        sampledExecutions++;


        records[currentSample]
          .workloadTimeUs =
          workloadTimeUs;


        records[currentSample]
          .monitored = true;


        records[currentSample]
          .mode = currentMode;


        // =========================================
        // DEADLINE CHECK
        // =========================================

        bool deadlineMiss =
          workloadTimeUs >
          (DEADLINE_MS * 1000ULL);


        records[currentSample]
          .deadlineMiss =
          deadlineMiss;


        if (deadlineMiss) {

          deadlineMissesDetected++;

          pressureCount++;

          recoveryCount = 0;
        }

        else {

          recoveryCount++;

          pressureCount = 0;
        }


        // =========================================
        // ADAPTIVE MODE SELECTION
        // =========================================

        MonitorMode previousMode =
          currentMode;


        // -----------------------------------------
        // PRESSURE INCREASE
        // -----------------------------------------

        if (
          pressureCount >=
          PRESSURE_THRESHOLD
        ) {

          pressureCount = 0;


          if (
            currentMode ==
            FULL_MODE
          ) {

            currentMode =
              MODERATE_MODE;
          }

          else if (
            currentMode ==
            MODERATE_MODE
          ) {

            currentMode =
              LOW_MODE;
          }

          else if (
            currentMode ==
            LOW_MODE
          ) {

            currentMode =
              MINIMAL_MODE;
          }
        }


        // -----------------------------------------
        // RECOVERY
        // -----------------------------------------

        if (
          recoveryCount >=
          RECOVERY_THRESHOLD
        ) {

          recoveryCount = 0;


          if (
            currentMode ==
            MINIMAL_MODE
          ) {

            currentMode =
              LOW_MODE;
          }

          else if (
            currentMode ==
            LOW_MODE
          ) {

            currentMode =
              MODERATE_MODE;
          }

          else if (
            currentMode ==
            MODERATE_MODE
          ) {

            currentMode =
              FULL_MODE;
          }
        }


        if (
          previousMode !=
          currentMode
        ) {

          modeChanges++;
        }


        // =========================================
        // WORKLOAD STATISTICS
        // =========================================

        if (
          workloadTimeUs <
          minWorkloadTime
        ) {

          minWorkloadTime =
            workloadTimeUs;
        }


        if (
          workloadTimeUs >
          maxWorkloadTime
        ) {

          maxWorkloadTime =
            workloadTimeUs;
        }


        sumWorkloadTime +=
          workloadTimeUs;


        sumWorkloadTimeSquared +=
          (double)workloadTimeUs *
          workloadTimeUs;
      }


      unsigned long long monitorFinish =
        esp_timer_get_time();


      monitorBusyTimeUs +=
        monitorFinish -
        monitorStart;


      monitorEventsProcessed++;
    }
  }
}


// =====================================================
// MONITORED HEAVY WORKLOAD
// CORE 0
// =====================================================

void monitoredTask(void *parameter) {

  TickType_t lastWakeTime =
    xTaskGetTickCount();


  // Warm-up
  vTaskDelay(
    pdMS_TO_TICKS(1000)
  );


  // ================================================
  // EXPERIMENT
  // ================================================

  for (
    int sample = 0;
    sample < NUM_SAMPLES;
    sample++
  ) {

    int samplingInterval =
      getSamplingInterval(
        currentMode
      );


    bool shouldMonitor =
      (
        sample %
        samplingInterval
      ) == 0;


    // Save mode used for this sample

    records[sample]
      .mode =
      currentMode;


    // =============================================
    // TOTAL MONITORED INTERVAL
    // =============================================

    unsigned long long totalStart =
      esp_timer_get_time();


    // =============================================
    // SEND START ONLY WHEN THIS EXECUTION IS SAMPLED
    // =============================================

    if (shouldMonitor) {

      MonitorEvent startEvent;

      startEvent.eventType =
        TASK_START_EVENT;

      startEvent.sampleNumber =
        sample;

      startEvent.timestampUs =
        esp_timer_get_time();


      xQueueSend(
        monitorQueue,
        &startEvent,
        portMAX_DELAY
      );
    }


    // =============================================
    // HEAVY WORKLOAD
    // =============================================

    digitalWrite(
      LED_PIN,
      HIGH
    );


    float x = 0.5;


    for (
      int i = 0;
      i < WORKLOAD_ITERATIONS;
      i++
    ) {

      x =
        sin(x) *
        cos(x) +
        sqrt(x + 1.0);
    }


    digitalWrite(
      LED_PIN,
      LOW
    );


    // =============================================
    // SEND FINISH EVENT
    // =============================================

    if (shouldMonitor) {

      MonitorEvent finishEvent;

      finishEvent.eventType =
        TASK_FINISH_EVENT;

      finishEvent.sampleNumber =
        sample;

      finishEvent.timestampUs =
        esp_timer_get_time();


      xQueueSend(
        monitorQueue,
        &finishEvent,
        portMAX_DELAY
      );
    }


    // =============================================
    // TOTAL MONITORED TIME
    // =============================================

    unsigned long long totalFinish =
      esp_timer_get_time();


    records[sample]
      .monitoredTimeUs =
      totalFinish -
      totalStart;


    records[sample]
      .monitored =
      shouldMonitor;


    // =============================================
    // COUNT COVERAGE
    // =============================================

    if (shouldMonitor) {

      switch (currentMode) {

        case FULL_MODE:
          fullSamples++;
          break;

        case MODERATE_MODE:
          moderateSamples++;
          break;

        case LOW_MODE:
          lowSamples++;
          break;

        case MINIMAL_MODE:
          minimalSamples++;
          break;
      }
    }


    // =============================================
    // PERIOD
    // =============================================

    vTaskDelayUntil(
      &lastWakeTime,
      pdMS_TO_TICKS(PERIOD_MS)
    );
  }


  // Allow monitor to process final events

  vTaskDelay(
    pdMS_TO_TICKS(500)
  );


  // ================================================
  // CALCULATE STATISTICS ONLY FOR SAMPLED EXECUTIONS
  // ================================================

  for (
    int i = 0;
    i < NUM_SAMPLES;
    i++
  ) {

    if (!records[i].monitored) {
      continue;
    }


    unsigned long workload =
      records[i].workloadTimeUs;


    unsigned long monitored =
      records[i].monitoredTimeUs;


    unsigned long overhead = 0;


    if (
      monitored >
      workload
    ) {

      overhead =
        monitored -
        workload;
    }


    records[i]
      .monitoringOverheadUs =
      overhead;


    sumMonitoredTime +=
      monitored;


    sumMonitoredTimeSquared +=
      (double)monitored *
      monitored;


    sumMonitoringOverhead +=
      overhead;
  }


  // ================================================
  // AVERAGES
  // ================================================

  double averageWorkloadTime =
    sumWorkloadTime /
    sampledExecutions;


  double workloadVariance =
    (
      sumWorkloadTimeSquared /
      sampledExecutions
    )
    -
    (
      averageWorkloadTime *
      averageWorkloadTime
    );


  if (workloadVariance < 0) {
    workloadVariance = 0;
  }


  double workloadStandardDeviation =
    sqrt(workloadVariance);


  double averageMonitoredTime =
    sumMonitoredTime /
    sampledExecutions;


  double monitoredVariance =
    (
      sumMonitoredTimeSquared /
      sampledExecutions
    )
    -
    (
      averageMonitoredTime *
      averageMonitoredTime
    );


  if (monitoredVariance < 0) {
    monitoredVariance = 0;
  }


  double monitoredStandardDeviation =
    sqrt(monitoredVariance);


  // ================================================
  // MONITORING OVERHEAD
  // ================================================

  double averageMonitoringOverhead =
    sumMonitoringOverhead /
    sampledExecutions;


  double overheadPercentage =
    (
      averageMonitoringOverhead /
      averageWorkloadTime
    ) * 100.0;


  // ================================================
  // COVERAGE
  // ================================================

  double monitoringCoverage =
    (
      (double)sampledExecutions /
      NUM_SAMPLES
    ) * 100.0;


  // ================================================
  // CPU UTILIZATION
  // ================================================

  double experimentTimeUs =
    NUM_SAMPLES *
    PERIOD_MS *
    1000.0;


  double taskCpuUtilization =
    (
      sumWorkloadTime /
      sampledExecutions /
      experimentTimeUs
    ) * 100.0;


  double monitorCpuUtilization =
    (
      monitorBusyTimeUs /
      experimentTimeUs
    ) * 100.0;


  // ================================================
  // MEMORY
  // ================================================

  size_t freeHeap =
    ESP.getFreeHeap();


  size_t minimumFreeHeap =
    ESP.getMinFreeHeap();


  UBaseType_t stackHighWaterMark =
    uxTaskGetStackHighWaterMark(
      NULL
    );


  // ================================================
  // RAW DATA
  // ================================================

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "RAW ADAPTIVE MONITOR DATA"
  );

  Serial.println(
    "========================================"
  );

  Serial.println(
    "Run,Monitored,Mode,Workload_ms,Monitored_ms,Overhead_ms,Deadline_Miss"
  );


  for (
    int i = 0;
    i < NUM_SAMPLES;
    i++
  ) {

    Serial.print(
      i + 1
    );

    Serial.print(",");


    Serial.print(
      records[i].monitored ?
      1 :
      0
    );

    Serial.print(",");


    switch (
      records[i].mode
    ) {

      case FULL_MODE:
        Serial.print("FULL");
        break;

      case MODERATE_MODE:
        Serial.print("MODERATE");
        break;

      case LOW_MODE:
        Serial.print("LOW");
        break;

      case MINIMAL_MODE:
        Serial.print("MINIMAL");
        break;
    }


    Serial.print(",");


    if (records[i].monitored) {

      Serial.print(
        records[i].workloadTimeUs /
        1000.0,
        3
      );

      Serial.print(",");

      Serial.print(
        records[i].monitoredTimeUs /
        1000.0,
        3
      );

      Serial.print(",");

      Serial.print(
        records[i].monitoringOverheadUs /
        1000.0,
        3
      );

      Serial.print(",");

      Serial.println(
        records[i].deadlineMiss ?
        1 :
        0
      );

    }

    else {

      Serial.println(
        "SKIPPED,SKIPPED,SKIPPED,SKIPPED"
      );
    }
  }


  // ================================================
  // FINAL SUMMARY
  // ================================================

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "PROJECT 1 - PART 3"
  );

  Serial.println(
    "EXPERIMENT 1 - ADAPTIVE HEAVY WORKLOAD"
  );

  Serial.println(
    "========================================"
  );


  Serial.print(
    "Samples: "
  );

  Serial.println(
    NUM_SAMPLES
  );


  Serial.print(
    "Workload iterations: "
  );

  Serial.println(
    WORKLOAD_ITERATIONS
  );


  Serial.print(
    "Period: "
  );

  Serial.print(
    PERIOD_MS
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Deadline: "
  );

  Serial.print(
    DEADLINE_MS
  );

  Serial.println(
    " ms"
  );


  // ================================================
  // ADAPTIVE PARAMETERS
  // ================================================

  Serial.println();

  Serial.print(
    "Pressure threshold: "
  );

  Serial.println(
    PRESSURE_THRESHOLD
  );


  Serial.print(
    "Recovery threshold: "
  );

  Serial.println(
    RECOVERY_THRESHOLD
  );


  Serial.println(
    "Sampling rates: FULL=1, MODERATE=2, LOW=5, MINIMAL=10"
  );


  // ================================================
  // WORKLOAD TIMING
  // ================================================

  Serial.println();

  Serial.print(
    "Minimum sampled workload execution time: "
  );

  Serial.print(
    minWorkloadTime / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Maximum sampled workload execution time: "
  );

  Serial.print(
    maxWorkloadTime / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Average sampled workload execution time: "
  );

  Serial.print(
    averageWorkloadTime / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Sampled workload timing standard deviation: "
  );

  Serial.print(
    workloadStandardDeviation / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  // ================================================
  // MONITORED TIMING
  // ================================================

  Serial.println();

  Serial.print(
    "Minimum sampled monitored execution time: "
  );

  Serial.print(
    records[0].monitored ?
    records[0].monitoredTimeUs / 1000.0 :
    0,
    3
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Average sampled monitored execution time: "
  );

  Serial.print(
    averageMonitoredTime / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Sampled monitored timing standard deviation: "
  );

  Serial.print(
    monitoredStandardDeviation / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  // ================================================
  // ADAPTIVE MONITORING
  // ================================================

  Serial.println();

  Serial.print(
    "Monitoring coverage: "
  );

  Serial.print(
    monitoringCoverage,
    2
  );

  Serial.println(
    " %"
  );


  Serial.print(
    "Full-mode monitored samples: "
  );

  Serial.println(
    fullSamples
  );


  Serial.print(
    "Moderate-mode monitored samples: "
  );

  Serial.println(
    moderateSamples
  );


  Serial.print(
    "Low-mode monitored samples: "
  );

  Serial.println(
    lowSamples
  );


  Serial.print(
    "Minimal-mode monitored samples: "
  );

  Serial.println(
    minimalSamples
  );


  Serial.print(
    "Mode changes: "
  );

  Serial.println(
    modeChanges
  );


  // ================================================
  // OVERHEAD
  // ================================================

  Serial.println();

  Serial.print(
    "Average monitoring overhead: "
  );

  Serial.print(
    averageMonitoringOverhead / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Monitoring overhead percentage: "
  );

  Serial.print(
    overheadPercentage,
    3
  );

  Serial.println(
    " %"
  );


  // ================================================
  // DETECTION
  // ================================================

  Serial.println();

  Serial.print(
    "Sampled deadline anomalies detected: "
  );

  Serial.println(
    deadlineMissesDetected
  );


  // ================================================
  // CPU
  // ================================================

  Serial.println();

  Serial.print(
    "Runtime monitor CPU utilization: "
  );

  Serial.print(
    monitorCpuUtilization,
    3
  );

  Serial.println(
    " %"
  );


  Serial.print(
    "Monitor events processed: "
  );

  Serial.println(
    monitorEventsProcessed
  );


  // ================================================
  // MEMORY
  // ================================================

  Serial.println();

  Serial.print(
    "Free heap: "
  );

  Serial.print(
    freeHeap
  );

  Serial.println(
    " bytes"
  );


  Serial.print(
    "Minimum free heap: "
  );

  Serial.print(
    minimumFreeHeap
  );

  Serial.println(
    " bytes"
  );


  Serial.print(
    "Monitor task stack high-water mark: "
  );

  Serial.print(
    stackHighWaterMark
  );

  Serial.println(
    " words"
  );


  Serial.println();

  Serial.println(
    "P3-E1 Adaptive Heavy Workload complete."
  );

  Serial.println(
    "========================================"
  );


  vTaskDelete(NULL);
}


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(
    115200
  );


  pinMode(
    LED_PIN,
    OUTPUT
  );


  digitalWrite(
    LED_PIN,
    LOW
  );


  // ================================================
  // CREATE QUEUE
  // ================================================

  monitorQueue =
    xQueueCreate(
      MONITOR_QUEUE_LENGTH,
      sizeof(MonitorEvent)
    );


  if (
    monitorQueue == NULL
  ) {

    Serial.println(
      "ERROR: Monitor queue creation failed."
    );

    while (true) {
      delay(1000);
    }
  }


  // ================================================
  // RUNTIME MONITOR → CORE 1
  // ================================================

  xTaskCreatePinnedToCore(

    runtimeMonitorTask,

    "AdaptiveMonitor",

    4096,

    NULL,

    2,

    NULL,

    1
  );


  // ================================================
  // MONITORED TASK → CORE 0
  // ================================================

  xTaskCreatePinnedToCore(

    monitoredTask,

    "HeavyWorkload",

    4096,

    NULL,

    1,

    NULL,

    0
  );
}


void loop() {

}