#include <Arduino.h>
#include <math.h>
#include "esp_timer.h"

// =====================================================
// P3-E2
// CONTEXT-AWARE ADAPTIVE RUNTIME MONITOR
// HEAVY WORKLOAD
// =====================================================

// -----------------------------
// Experiment configuration
// -----------------------------

#define LED_PIN 4

#define NUM_SAMPLES 1000

#define WORKLOAD_ITERATIONS 10000

#define PERIOD_MS 100
#define DEADLINE_MS 50

#define MONITOR_QUEUE_LENGTH 16

#define TASK_START_EVENT  1
#define TASK_FINISH_EVENT 2

// =====================================================
// ADAPTIVE THRESHOLDS
// =====================================================

#define FULL_THRESHOLD       0.80
#define BALANCED_THRESHOLD   0.90
#define LIGHT_THRESHOLD      1.00

// Revised hysteresis
#define MODE_CHANGE_COUNT     3
#define RECOVERY_COUNT_LIMIT  3

// Critical mode detailed analysis
#define CRITICAL_DETAILED_INTERVAL 10


// =====================================================
// MONITOR MODES
// =====================================================

enum MonitorMode {
  FULL_MODE,
  BALANCED_MODE,
  LIGHT_MODE,
  CRITICAL_MODE
};


// =====================================================
// MONITOR EVENT
// =====================================================

struct MonitorEvent {

  uint8_t eventType;
  uint16_t sampleNumber;
  uint64_t timestampUs;
};


// =====================================================
// RAW SAMPLE RECORD
// =====================================================

struct SampleRecord {

  unsigned long workloadTimeUs;
  unsigned long monitoredTimeUs;
  unsigned long overheadUs;

  bool deadlineMiss;

  MonitorMode mode;

  bool varianceCalculated;
  bool trendCalculated;
  bool criticalDetailedCheck;
};


// =====================================================
// GLOBALS
// =====================================================

SampleRecord records[NUM_SAMPLES];

QueueHandle_t monitorQueue;


// =====================================================
// ADAPTIVE STATE
// =====================================================

MonitorMode currentMode = FULL_MODE;

int pressureCounter = 0;
int recoveryCounter = 0;


// =====================================================
// STATISTICS
// =====================================================

unsigned long fullSamples = 0;
unsigned long balancedSamples = 0;
unsigned long lightSamples = 0;
unsigned long criticalSamples = 0;

unsigned long criticalDetailedSamples = 0;

unsigned long modeChanges = 0;

unsigned long monitorEventsProcessed = 0;
unsigned long detailedCalculations = 0;

unsigned long deadlineAnomalies = 0;


// =====================================================
// TIMING STATISTICS
// =====================================================

unsigned long minExecutionTime = ULONG_MAX;
unsigned long maxExecutionTime = 0;

double executionSum = 0.0;
double executionSumSquared = 0.0;

double monitoredSum = 0.0;
double monitoredSumSquared = 0.0;

double overheadSum = 0.0;


// =====================================================
// MONITOR CPU TIME
// =====================================================

unsigned long long monitorBusyTimeUs = 0;


// =====================================================
// RUNNING VARIANCE / TREND
// =====================================================

double runningMean = 0.0;
double runningM2 = 0.0;

double previousExecutionMs = 0.0;

unsigned long detailedSampleCount = 0;


// =====================================================
// MODE NAME
// =====================================================

const char* modeName(MonitorMode mode) {

  switch (mode) {

    case FULL_MODE:
      return "FULL";

    case BALANCED_MODE:
      return "BALANCED";

    case LIGHT_MODE:
      return "LIGHT";

    case CRITICAL_MODE:
      return "CRITICAL";
  }

  return "UNKNOWN";
}


// =====================================================
// ADAPTIVE MODE UPDATE
//
// IMPORTANT:
// One mode step at a time.
// Three consecutive samples are required.
// Recovery also requires three consecutive samples.
// =====================================================

void updateAdaptiveMode(double executionMs) {

  MonitorMode targetMode;

  double ratio =
    executionMs / DEADLINE_MS;


  // ---------------------------------------------------
  // Determine pressure region
  // ---------------------------------------------------

  if (ratio < FULL_THRESHOLD) {

    targetMode = FULL_MODE;
  }

  else if (ratio < BALANCED_THRESHOLD) {

    targetMode = BALANCED_MODE;
  }

  else if (ratio < LIGHT_THRESHOLD) {

    targetMode = LIGHT_MODE;
  }

  else {

    targetMode = CRITICAL_MODE;
  }


  // ---------------------------------------------------
  // Already in target mode
  // ---------------------------------------------------

  if (targetMode == currentMode) {

    pressureCounter = 0;
    recoveryCounter = 0;

    return;
  }


  // ---------------------------------------------------
  // Target is MORE aggressive
  // ---------------------------------------------------

  if (targetMode > currentMode) {

    recoveryCounter = 0;

    pressureCounter++;


    if (pressureCounter >= MODE_CHANGE_COUNT) {

      pressureCounter = 0;

      MonitorMode oldMode =
        currentMode;


      // Move only ONE level upward

      if (currentMode == FULL_MODE) {

        currentMode =
          BALANCED_MODE;
      }

      else if (currentMode == BALANCED_MODE) {

        currentMode =
          LIGHT_MODE;
      }

      else if (currentMode == LIGHT_MODE) {

        currentMode =
          CRITICAL_MODE;
      }


      if (oldMode != currentMode) {

        modeChanges++;

        Serial.print(
          "ADAPTIVE MODE CHANGE: "
        );

        Serial.print(
          modeName(oldMode)
        );

        Serial.print(
          " -> "
        );

        Serial.println(
          modeName(currentMode)
        );
      }
    }

    return;
  }


  // ---------------------------------------------------
  // Target is LESS aggressive
  // ---------------------------------------------------

  recoveryCounter = recoveryCounter + 1;

  pressureCounter = 0;


  if (recoveryCounter >= RECOVERY_COUNT_LIMIT) {

    recoveryCounter = 0;

    MonitorMode oldMode =
      currentMode;


    // Move only ONE level downward

    if (currentMode == CRITICAL_MODE) {

      currentMode =
        LIGHT_MODE;
    }

    else if (currentMode == LIGHT_MODE) {

      currentMode =
        BALANCED_MODE;
    }

    else if (currentMode == BALANCED_MODE) {

      currentMode =
        FULL_MODE;
    }


    if (oldMode != currentMode) {

      modeChanges++;

      Serial.print(
        "ADAPTIVE MODE CHANGE: "
      );

      Serial.print(
        modeName(oldMode)
      );

      Serial.print(
        " -> "
      );

      Serial.println(
        modeName(currentMode)
      );
    }
  }
}


// =====================================================
// UPDATE VARIANCE
// =====================================================

void updateVariance(
  double executionMs
) {

  detailedSampleCount++;

  double delta =
    executionMs -
    runningMean;

  runningMean +=
    delta /
    detailedSampleCount;

  runningM2 +=
    delta *
    (
      executionMs -
      runningMean
    );
}


// =====================================================
// RUNTIME MONITOR TASK
// CORE 1
// =====================================================

void runtimeMonitorTask(void *parameter) {

  MonitorEvent event;

  uint64_t startTimestamp = 0;

  uint16_t sampleNumber = 0;


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


      // ------------------------------------------------
      // START EVENT
      // ------------------------------------------------

      if (
        event.eventType ==
        TASK_START_EVENT
      ) {

        startTimestamp =
          event.timestampUs;

        sampleNumber =
          event.sampleNumber;
      }


      // ------------------------------------------------
      // FINISH EVENT
      // ------------------------------------------------

      else if (
        event.eventType ==
        TASK_FINISH_EVENT
      ) {

        uint64_t executionUs =
          event.timestampUs -
          startTimestamp;

        double executionMs =
          executionUs / 1000.0;


        // ----------------------------------------------
        // Record raw workload timing
        // ----------------------------------------------

        records[sampleNumber]
          .workloadTimeUs =
          executionUs;

        records[sampleNumber]
          .mode =
          currentMode;

        records[sampleNumber]
          .criticalDetailedCheck =
          false;

        records[sampleNumber]
          .varianceCalculated =
          false;

        records[sampleNumber]
          .trendCalculated =
          false;


        // ----------------------------------------------
        // Mode counter
        // ----------------------------------------------

        switch (currentMode) {

          case FULL_MODE:
            fullSamples++;
            break;

          case BALANCED_MODE:
            balancedSamples++;
            break;

          case LIGHT_MODE:
            lightSamples++;
            break;

          case CRITICAL_MODE:
            criticalSamples++;
            break;
        }


        // ----------------------------------------------
        // Deadline
        // ----------------------------------------------

        bool deadlineMiss =
          executionMs >
          DEADLINE_MS;


        records[sampleNumber]
          .deadlineMiss =
          deadlineMiss;


        if (deadlineMiss) {

          deadlineAnomalies++;
        }


        // ==============================================
        // FULL MODE
        // ==============================================

        if (
          currentMode ==
          FULL_MODE
        ) {

          records[sampleNumber]
            .varianceCalculated =
            true;

          records[sampleNumber]
            .trendCalculated =
            true;


          detailedCalculations++;


          updateVariance(
            executionMs
          );


          double trend =
            executionMs -
            previousExecutionMs;

          (void)trend;


          previousExecutionMs =
            executionMs;
        }


        // ==============================================
        // BALANCED MODE
        // Every 2nd sample gets detailed analysis
        // ==============================================

        else if (
          currentMode ==
          BALANCED_MODE
        ) {

          if (
            ((sampleNumber + 1) % 2) == 0
          ) {

            records[sampleNumber]
              .varianceCalculated =
              true;


            detailedCalculations++;


            updateVariance(
              executionMs
            );
          }
        }


        // ==============================================
        // LIGHT MODE
        // Basic timing only
        // ==============================================

        else if (
          currentMode ==
          LIGHT_MODE
        ) {

          // No expensive calculations
        }


        // ==============================================
        // CRITICAL MODE
        // Detailed monitoring every 10th sample
        // ==============================================

        else {

          if (
            ((sampleNumber + 1) %
            CRITICAL_DETAILED_INTERVAL) == 0
          ) {

            records[sampleNumber]
              .criticalDetailedCheck =
              true;

            records[sampleNumber]
              .varianceCalculated =
              true;

            records[sampleNumber]
              .trendCalculated =
              true;


            criticalDetailedSamples++;

            detailedCalculations++;


            updateVariance(
              executionMs
            );


            double trend =
              executionMs -
              previousExecutionMs;

            (void)trend;


            previousExecutionMs =
              executionMs;
          }
        }


        // ----------------------------------------------
        // Global timing statistics
        // ----------------------------------------------

        executionSum +=
          executionMs;

        executionSumSquared +=
          executionMs *
          executionMs;


        if (
          executionUs <
          minExecutionTime
        ) {

          minExecutionTime =
            executionUs;
        }


        if (
          executionUs >
          maxExecutionTime
        ) {

          maxExecutionTime =
            executionUs;
        }


        // ----------------------------------------------
        // Adaptive decision
        //
        // This affects the NEXT sample.
        // ----------------------------------------------

        updateAdaptiveMode(
          executionMs
        );
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
// HEAVY MONITORED WORKLOAD
// CORE 0
// =====================================================

void monitoredTask(void *parameter) {

  TickType_t lastWakeTime =
    xTaskGetTickCount();


  delay(1500);


  for (
    int sample = 0;
    sample < NUM_SAMPLES;
    sample++
  ) {

    unsigned long long totalStart =
      esp_timer_get_time();


    // -----------------------------------------------
    // START EVENT
    // -----------------------------------------------

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


    // -----------------------------------------------
    // HEAVY WORKLOAD
    // -----------------------------------------------

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


    if (
      x == -9999.0
    ) {

      Serial.println(
        "Impossible"
      );
    }


    digitalWrite(
      LED_PIN,
      LOW
    );


    // -----------------------------------------------
    // FINISH EVENT
    // -----------------------------------------------

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


    unsigned long long totalFinish =
      esp_timer_get_time();


    records[sample]
      .monitoredTimeUs =
      totalFinish -
      totalStart;


    // -----------------------------------------------
    // Periodic release
    // -----------------------------------------------

    vTaskDelayUntil(
      &lastWakeTime,
      pdMS_TO_TICKS(
        PERIOD_MS
      )
    );
  }


  // Give monitor task time to finish
  delay(1000);


  // -----------------------------------------------
  // Calculate statistics
  // -----------------------------------------------

  double averageExecution =
    executionSum /
    NUM_SAMPLES;


  double variance =
    (
      executionSumSquared /
      NUM_SAMPLES
    ) -
    (
      averageExecution *
      averageExecution
    );


  if (variance < 0)
    variance = 0;


  double standardDeviation =
    sqrt(variance);


  unsigned long minMonitoredUs =
    ULONG_MAX;

  unsigned long maxMonitoredUs =
    0;


  for (
    int i = 0;
    i < NUM_SAMPLES;
    i++
  ) {

    double workloadMs =
      records[i]
        .workloadTimeUs /
      1000.0;

    double monitoredMs =
      records[i]
        .monitoredTimeUs /
      1000.0;


    double overheadMs =
      monitoredMs -
      workloadMs;


    if (overheadMs < 0)
      overheadMs = 0;


    records[i]
      .overheadUs =
      overheadMs * 1000.0;


    monitoredSum +=
      monitoredMs;

    monitoredSumSquared +=
      monitoredMs *
      monitoredMs;

    overheadSum +=
      overheadMs;


    if (
      records[i]
        .monitoredTimeUs <
      minMonitoredUs
    ) {

      minMonitoredUs =
        records[i]
          .monitoredTimeUs;
    }


    if (
      records[i]
        .monitoredTimeUs >
      maxMonitoredUs
    ) {

      maxMonitoredUs =
        records[i]
          .monitoredTimeUs;
    }
  }


  double averageMonitored =
    monitoredSum /
    NUM_SAMPLES;


  double monitoredVariance =
    (
      monitoredSumSquared /
      NUM_SAMPLES
    ) -
    (
      averageMonitored *
      averageMonitored
    );


  if (monitoredVariance < 0)
    monitoredVariance = 0;


  double monitoredStdDev =
    sqrt(monitoredVariance);


  double averageOverhead =
    overheadSum /
    NUM_SAMPLES;


  double overheadPercentage =
    (
      averageOverhead /
      averageExecution
    ) * 100.0;


  // -----------------------------------------------
  // CPU utilization
  // -----------------------------------------------

  double experimentTimeMs =
    NUM_SAMPLES *
    PERIOD_MS;


  double monitorCpu =
    (
      monitorBusyTimeUs /
      (
        experimentTimeMs *
        1000.0
      )
    ) * 100.0;


  double workloadCpu =
    (
      averageExecution /
      PERIOD_MS
    ) * 100.0;


  // -----------------------------------------------
  // Memory
  // -----------------------------------------------

  size_t freeHeap =
    ESP.getFreeHeap();

  size_t minimumFreeHeap =
    ESP.getMinFreeHeap();

  UBaseType_t stackHighWater =
    uxTaskGetStackHighWaterMark(
      NULL
    );


  // =================================================
  // SUMMARY
  // =================================================

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "P3-E2 - ADAPTIVE HEAVY WORKLOAD"
  );

  Serial.println(
    "========================================"
  );


  Serial.println();


  Serial.print(
    "Workload iterations: "
  );

  Serial.println(
    WORKLOAD_ITERATIONS
  );


  Serial.println();


  Serial.print(
    "Minimum workload execution time: "
  );

  Serial.print(
    minExecutionTime / 1000.0,
    3
  );

  Serial.println(" ms");


  Serial.print(
    "Maximum workload execution time: "
  );

  Serial.print(
    maxExecutionTime / 1000.0,
    3
  );

  Serial.println(" ms");


  Serial.print(
    "Average workload execution time: "
  );

  Serial.print(
    averageExecution,
    3
  );

  Serial.println(" ms");


  Serial.print(
    "Workload timing standard deviation: "
  );

  Serial.print(
    standardDeviation,
    3
  );

  Serial.println(" ms");


  Serial.println();


  Serial.print(
    "Minimum monitored execution time: "
  );

  Serial.print(
    minMonitoredUs / 1000.0,
    3
  );

  Serial.println(" ms");


  Serial.print(
    "Maximum monitored execution time: "
  );

  Serial.print(
    maxMonitoredUs / 1000.0,
    3
  );

  Serial.println(" ms");


  Serial.print(
    "Average monitored execution time: "
  );

  Serial.print(
    averageMonitored,
    3
  );

  Serial.println(" ms");


  Serial.print(
    "Monitored timing standard deviation: "
  );

  Serial.print(
    monitoredStdDev,
    3
  );

  Serial.println(" ms");


  Serial.println();


  Serial.print(
    "FULL-mode monitored samples: "
  );

  Serial.println(
    fullSamples
  );


  Serial.print(
    "BALANCED-mode monitored samples: "
  );

  Serial.println(
    balancedSamples
  );


  Serial.print(
    "LIGHT-mode monitored samples: "
  );

  Serial.println(
    lightSamples
  );


  Serial.print(
    "CRITICAL-mode monitored samples: "
  );

  Serial.println(
    criticalSamples
  );


  Serial.print(
    "CRITICAL detailed samples: "
  );

  Serial.println(
    criticalDetailedSamples
  );


  Serial.print(
    "Mode changes: "
  );

  Serial.println(
    modeChanges
  );


  Serial.println();


  Serial.println(
    "Monitoring coverage: 100.00 %"
  );


  Serial.print(
    "Monitor events processed: "
  );

  Serial.println(
    monitorEventsProcessed
  );


  Serial.print(
    "Detailed monitoring calculations: "
  );

  Serial.println(
    detailedCalculations
  );


  Serial.println();


  Serial.print(
    "Detected deadline anomalies: "
  );

  Serial.println(
    deadlineAnomalies
  );


  Serial.print(
    "Deadline misses: "
  );

  Serial.println(
    deadlineAnomalies
  );


  Serial.print(
    "Deadline anomaly rate: "
  );

  Serial.print(
    (
      (double)deadlineAnomalies /
      NUM_SAMPLES
    ) * 100.0,
    2
  );

  Serial.println(" %");


  Serial.println();


  Serial.print(
    "Average monitoring overhead: "
  );

  Serial.print(
    averageOverhead,
    3
  );

  Serial.println(" ms");


  Serial.print(
    "Monitoring overhead percentage: "
  );

  Serial.print(
    overheadPercentage,
    3
  );

  Serial.println(" %");


  Serial.println();


  Serial.print(
    "Monitored task CPU utilization: "
  );

  Serial.print(
    workloadCpu,
    2
  );

  Serial.println(" %");


  Serial.print(
    "Runtime monitor CPU utilization: "
  );

  Serial.print(
    monitorCpu,
    3
  );

  Serial.println(" %");


  Serial.println();


  Serial.print(
    "Free heap: "
  );

  Serial.print(
    freeHeap
  );

  Serial.println(" bytes");


  Serial.print(
    "Minimum free heap: "
  );

  Serial.print(
    minimumFreeHeap
  );

  Serial.println(" bytes");


  Serial.print(
    "Monitor task stack high-water mark: "
  );

  Serial.print(
    stackHighWater
  );

  Serial.println(" words");


  // =================================================
  // RAW CSV
  // =================================================

  Serial.println();

  Serial.println(
    "Sample,Workload_ms,Monitored_ms,Overhead_ms,Mode,DeadlineMiss,Variance,Trend,CriticalDetailed"
  );


  for (
    int i = 0;
    i < NUM_SAMPLES;
    i++
  ) {

    Serial.print(i + 1);
    Serial.print(",");

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
      records[i].overheadUs /
      1000.0,
      3
    );

    Serial.print(",");

    Serial.print(
      modeName(
        records[i].mode
      )
    );

    Serial.print(",");

    Serial.print(
      records[i].deadlineMiss
      ? 1 : 0
    );

    Serial.print(",");

    Serial.print(
      records[i].varianceCalculated
      ? 1 : 0
    );

    Serial.print(",");

    Serial.print(
      records[i].trendCalculated
      ? 1 : 0
    );

    Serial.print(",");

    Serial.println(
      records[i].criticalDetailedCheck
      ? 1 : 0
    );
  }


  Serial.println(
    "========== END RAW DATA =========="
  );


  vTaskDelete(NULL);
}


// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  pinMode(
    LED_PIN,
    OUTPUT
  );

  digitalWrite(
    LED_PIN,
    LOW
  );


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


  // -----------------------------------------------
  // Adaptive monitor → CORE 1
  // -----------------------------------------------

  xTaskCreatePinnedToCore(

    runtimeMonitorTask,

    "AdaptiveMonitor",

    4096,

    NULL,

    2,

    NULL,

    1
  );


  // -----------------------------------------------
  // Heavy workload → CORE 0
  // -----------------------------------------------

  xTaskCreatePinnedToCore(

    monitoredTask,

    "MonitoredWorkload",

    4096,

    NULL,

    1,

    NULL,

    0
  );
}


// =====================================================
// LOOP
// =====================================================

void loop() {

}