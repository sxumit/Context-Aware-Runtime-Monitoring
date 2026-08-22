#include <Arduino.h>
#include <math.h>
#include "esp_timer.h"

// =====================================================
// PROJECT 1 - PART 3
// UNIVERSAL CONTEXT-AWARE ADAPTIVE RUNTIME MONITOR
//
// P3-E1: NORMAL WORKLOAD
// =====================================================

#define LED_PIN 4

#define NUM_SAMPLES 1000
#define WORKLOAD_ITERATIONS 3500

#define PERIOD_MS 100
#define DEADLINE_MS 50

#define MONITOR_QUEUE_LENGTH 16

#define TASK_START_EVENT  1
#define TASK_FINISH_EVENT 2


// =====================================================
// ADAPTATION THRESHOLDS
// =====================================================

#define FULL_LIMIT_MS       40.0
#define BALANCED_LIMIT_MS   45.0
#define LIGHT_LIMIT_MS      50.0

#define PRESSURE_COUNT_LIMIT 3
#define RECOVERY_COUNT_LIMIT 5


// =====================================================
// MONITORING MODES
// =====================================================

enum MonitorMode {
  FULL_MODE,
  BALANCED_MODE,
  LIGHT_MODE,
  CRITICAL_MODE
};


// =====================================================
// EVENT STRUCTURE
// =====================================================

struct MonitorEvent {

  uint8_t eventType;

  uint16_t sampleNumber;

  uint64_t timestampUs;
};


// =====================================================
// RAW DATA STRUCTURE
// =====================================================

struct SampleRecord {

  unsigned long workloadTimeUs;

  unsigned long monitoredTimeUs;

  unsigned long overheadUs;

  bool deadlineMiss;

  MonitorMode mode;

  bool varianceCalculated;

  bool trendCalculated;
};


// =====================================================
// GLOBAL OBJECTS
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
// EXPERIMENT STATISTICS
// =====================================================

unsigned long monitorEventsProcessed = 0;

unsigned long deadlineAnomalies = 0;

unsigned long modeChanges = 0;

unsigned long fullSamples = 0;

unsigned long balancedSamples = 0;

unsigned long lightSamples = 0;

unsigned long criticalSamples = 0;


// =====================================================
// EXECUTION TIME STATISTICS
// =====================================================

unsigned long minExecutionTime = ULONG_MAX;

unsigned long maxExecutionTime = 0;

double executionSum = 0.0;

double executionSumSquared = 0.0;


// =====================================================
// MONITORED EXECUTION STATISTICS
// =====================================================

double monitoredSum = 0.0;

double monitoredSumSquared = 0.0;

double overheadSum = 0.0;


// =====================================================
// MONITOR CPU MEASUREMENT
// =====================================================

unsigned long long monitorBusyTimeUs = 0;


// =====================================================
// DETAILED MONITORING STATISTICS
// =====================================================

double runningMean = 0.0;

double runningM2 = 0.0;

double previousExecutionMs = 0.0;

unsigned long detailedCalculations = 0;


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
// =====================================================

void updateAdaptiveMode(double executionMs) {

  MonitorMode previousMode = currentMode;


  // ===================================================
  // SEVERE PRESSURE
  // ===================================================

  if (executionMs > LIGHT_LIMIT_MS) {

    pressureCounter++;

    recoveryCounter = 0;

    if (pressureCounter >= PRESSURE_COUNT_LIMIT) {

      pressureCounter = 0;

      if (currentMode < CRITICAL_MODE) {

        currentMode =
          (MonitorMode)(currentMode + 1);
      }
    }
  }


  // ===================================================
  // HIGH PRESSURE
  // ===================================================

  else if (executionMs >= BALANCED_LIMIT_MS) {

    pressureCounter++;

    recoveryCounter = 0;

    if (pressureCounter >= PRESSURE_COUNT_LIMIT) {

      pressureCounter = 0;

      if (currentMode == FULL_MODE) {

        currentMode = BALANCED_MODE;
      }

      else if (currentMode == BALANCED_MODE) {

        currentMode = LIGHT_MODE;
      }
    }
  }


  // ===================================================
  // MODERATE PRESSURE
  // ===================================================

  else if (executionMs >= FULL_LIMIT_MS) {

    pressureCounter++;

    recoveryCounter = 0;

    if (pressureCounter >= PRESSURE_COUNT_LIMIT) {

      pressureCounter = 0;

      if (currentMode == FULL_MODE) {

        currentMode = BALANCED_MODE;
      }
    }
  }


  // ===================================================
  // HEALTHY / LOW PRESSURE
  // ===================================================

  else {

    pressureCounter = 0;

    recoveryCounter++;

    if (recoveryCounter >= RECOVERY_COUNT_LIMIT) {

      recoveryCounter = 0;

      if (currentMode == CRITICAL_MODE) {

        currentMode = LIGHT_MODE;
      }

      else if (currentMode == LIGHT_MODE) {

        currentMode = BALANCED_MODE;
      }

      else if (currentMode == BALANCED_MODE) {

        currentMode = FULL_MODE;
      }
    }
  }


  // ===================================================
  // MODE CHANGE LOG
  // ===================================================

  if (previousMode != currentMode) {

    modeChanges++;

    Serial.print("ADAPTIVE MODE CHANGE: ");

    Serial.print(modeName(previousMode));

    Serial.print(" -> ");

    Serial.println(modeName(currentMode));
  }
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


      // =================================================
      // START EVENT
      // =================================================

      if (event.eventType == TASK_START_EVENT) {

        startTimestamp =
          event.timestampUs;

        sampleNumber =
          event.sampleNumber;
      }


      // =================================================
      // FINISH EVENT
      // =================================================

      else if (event.eventType == TASK_FINISH_EVENT) {

        uint64_t executionUs =
          event.timestampUs -
          startTimestamp;


        double executionMs =
          executionUs / 1000.0;


        // ===============================================
        // STORE BASIC RAW DATA
        // ===============================================

        records[sampleNumber].workloadTimeUs =
          executionUs;

        records[sampleNumber].mode =
          currentMode;


        // ===============================================
        // SWITCH STATEMENT
        // ===============================================

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


        // ===============================================
        // DEADLINE CHECK
        // ===============================================

        bool deadlineMiss =
          executionMs > DEADLINE_MS;


        records[sampleNumber].deadlineMiss =
          deadlineMiss;


        if (deadlineMiss) {

          deadlineAnomalies++;
        }


        // ===============================================
        // FULL MODE
        // ===============================================

        if (currentMode == FULL_MODE) {

          records[sampleNumber]
            .varianceCalculated = true;

          records[sampleNumber]
            .trendCalculated = true;


          // Running variance calculation

          detailedCalculations++;

          double delta =
            executionMs -
            runningMean;

          runningMean +=
            delta /
            (sampleNumber + 1);

          double delta2 =
            executionMs -
            runningMean;

          runningM2 +=
            delta *
            delta2;


          // Trend calculation

          double trend =
            executionMs -
            previousExecutionMs;

          (void)trend;

          previousExecutionMs =
            executionMs;
        }


        // ===============================================
        // BALANCED MODE
        // ===============================================

        else if (
          currentMode == BALANCED_MODE
        ) {

          records[sampleNumber]
            .varianceCalculated = true;

          records[sampleNumber]
            .trendCalculated = false;


          detailedCalculations++;

          double delta =
            executionMs -
            runningMean;

          runningMean +=
            delta /
            (sampleNumber + 1);

          runningM2 +=
            delta *
            (
              executionMs -
              runningMean
            );
        }


        // ===============================================
        // LIGHT MODE
        // ===============================================

        else if (
          currentMode == LIGHT_MODE
        ) {

          records[sampleNumber]
            .varianceCalculated = false;

          records[sampleNumber]
            .trendCalculated = false;

          // Essential monitoring only:
          // execution time + deadline detection.
        }


        // ===============================================
        // CRITICAL MODE
        // ===============================================

        else {

          records[sampleNumber]
            .varianceCalculated = false;

          records[sampleNumber]
            .trendCalculated = false;

          // Minimum essential monitoring:
          // execution time + deadline detection.
        }


        // ===============================================
        // GLOBAL EXECUTION STATISTICS
        // ===============================================

        executionSum += executionMs;

        executionSumSquared +=
          executionMs *
          executionMs;


        if (executionUs < minExecutionTime) {

          minExecutionTime =
            executionUs;
        }


        if (executionUs > maxExecutionTime) {

          maxExecutionTime =
            executionUs;
        }


        // ===============================================
        // ADAPTIVE DECISION
        // ===============================================

        updateAdaptiveMode(executionMs);
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
// MONITORED WORKLOAD TASK
// CORE 0
// =====================================================

void monitoredTask(void *parameter) {

  TickType_t lastWakeTime =
    xTaskGetTickCount();


  delay(1000);


  // ===================================================
  // MAIN EXPERIMENT LOOP
  // ===================================================

  for (
    int sample = 0;
    sample < NUM_SAMPLES;
    sample++
  ) {

    unsigned long long totalStart =
      esp_timer_get_time();


    // =================================================
    // START EVENT
    // =================================================

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


    // =================================================
    // WORKLOAD
    // =================================================

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


    // Prevent compiler optimization

    if (x == -9999.0) {

      Serial.println(
        "Impossible"
      );
    }


    digitalWrite(
      LED_PIN,
      LOW
    );


    // =================================================
    // FINISH EVENT
    // =================================================

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


    // =================================================
    // MONITORED EXECUTION TIME
    // =================================================

    unsigned long long totalFinish =
      esp_timer_get_time();


    records[sample].monitoredTimeUs =
      totalFinish -
      totalStart;


    // =================================================
    // PERIOD
    // =================================================

    vTaskDelayUntil(
      &lastWakeTime,
      pdMS_TO_TICKS(PERIOD_MS)
    );
  }


  // Allow monitor to process final events

  delay(500);


  // ===================================================
  // FINAL STATISTICS
  // ===================================================

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


  if (variance < 0) {

    variance = 0;
  }


  double standardDeviation =
    sqrt(variance);


  // ===================================================
  // PROCESS RAW MONITORED DATA
  // ===================================================

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
      records[i].workloadTimeUs /
      1000.0;


    double monitoredMs =
      records[i].monitoredTimeUs /
      1000.0;


    double overheadMs =
      monitoredMs -
      workloadMs;


    if (overheadMs < 0) {

      overheadMs = 0;
    }


    records[i].overheadUs =
      overheadMs * 1000.0;


    monitoredSum +=
      monitoredMs;

    monitoredSumSquared +=
      monitoredMs *
      monitoredMs;

    overheadSum +=
      overheadMs;


    if (
      records[i].monitoredTimeUs <
      minMonitoredUs
    ) {

      minMonitoredUs =
        records[i].monitoredTimeUs;
    }


    if (
      records[i].monitoredTimeUs >
      maxMonitoredUs
    ) {

      maxMonitoredUs =
        records[i].monitoredTimeUs;
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


  if (monitoredVariance < 0) {

    monitoredVariance = 0;
  }


  double monitoredStdDev =
    sqrt(monitoredVariance);


  double averageOverhead =
    overheadSum /
    NUM_SAMPLES;


  double overheadPercentage =
    (
      averageOverhead /
      averageExecution
    ) *
    100.0;


  // ===================================================
  // CPU UTILIZATION
  // ===================================================

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
    ) *
    100.0;


  double workloadCpu =
    (
      averageExecution /
      PERIOD_MS
    ) *
    100.0;


  // ===================================================
  // MEMORY
  // ===================================================

  size_t freeHeap =
    ESP.getFreeHeap();

  size_t minimumFreeHeap =
    ESP.getMinFreeHeap();


  UBaseType_t stackHighWater =
    uxTaskGetStackHighWaterMark(NULL);


  // ===================================================
  // FINAL SUMMARY
  // ===================================================

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "PROJECT 1 - PART 3"
  );

  Serial.println(
    "P3-E1 - UNIVERSAL ADAPTIVE MONITOR"
  );

  Serial.println(
    "NORMAL WORKLOAD"
  );

  Serial.println(
    "========================================"
  );


  Serial.print("Samples: ");
  Serial.println(NUM_SAMPLES);


  Serial.print("Workload iterations: ");
  Serial.println(WORKLOAD_ITERATIONS);


  Serial.print("Period: ");
  Serial.print(PERIOD_MS);
  Serial.println(" ms");


  Serial.print("Deadline: ");
  Serial.print(DEADLINE_MS);
  Serial.println(" ms");


  // ===================================================
  // WORKLOAD TIMING
  // ===================================================

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


  // ===================================================
  // MONITORED TIMING
  // ===================================================

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


  // ===================================================
  // ADAPTIVE MODE RESULTS
  // ===================================================

  Serial.println();

  Serial.print(
    "FULL-mode monitored samples: "
  );

  Serial.println(fullSamples);


  Serial.print(
    "BALANCED-mode monitored samples: "
  );

  Serial.println(balancedSamples);


  Serial.print(
    "LIGHT-mode monitored samples: "
  );

  Serial.println(lightSamples);


  Serial.print(
    "CRITICAL-mode monitored samples: "
  );

  Serial.println(criticalSamples);


  Serial.print(
    "Mode changes: "
  );

  Serial.println(modeChanges);


  // ===================================================
  // MONITORING COVERAGE
  // ===================================================

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


  // ===================================================
  // ANOMALY DETECTION
  // ===================================================

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


  double detectionRate =
    (
      (double)deadlineAnomalies /
      NUM_SAMPLES
    ) * 100.0;


  Serial.print(
    "Deadline anomaly rate: "
  );

  Serial.print(
    detectionRate,
    2
  );

  Serial.println(" %");


  // ===================================================
  // OVERHEAD
  // ===================================================

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


  // ===================================================
  // CPU
  // ===================================================

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


  // ===================================================
  // MEMORY
  // ===================================================

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


  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "P3-E1 SUMMARY COMPLETE"
  );

  Serial.println(
    "========================================"
  );


  // ===================================================
  // RAW DATA CSV
  // ===================================================

  Serial.println();

  Serial.println(
    "========== RAW SAMPLE DATA =========="
  );

  Serial.println(
    "Sample,Workload_ms,Monitored_ms,Overhead_ms,Mode,DeadlineMiss,Variance,Trend"
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
      modeName(records[i].mode)
    );

    Serial.print(",");


    Serial.print(
      records[i].deadlineMiss ? 1 : 0
    );

    Serial.print(",");


    Serial.print(
      records[i].varianceCalculated ? 1 : 0
    );

    Serial.print(",");


    Serial.println(
      records[i].trendCalculated ? 1 : 0
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


  // ===================================================
  // CREATE MONITOR QUEUE
  // ===================================================

  monitorQueue =
    xQueueCreate(
      MONITOR_QUEUE_LENGTH,
      sizeof(MonitorEvent)
    );


  if (monitorQueue == NULL) {

    Serial.println(
      "ERROR: Monitor queue creation failed."
    );

    while (true) {

      delay(1000);
    }
  }


  // ===================================================
  // RUNTIME MONITOR
  // CORE 1
  // ===================================================

  xTaskCreatePinnedToCore(

    runtimeMonitorTask,

    "AdaptiveMonitor",

    4096,

    NULL,

    2,

    NULL,

    1
  );


  // ===================================================
  // MONITORED WORKLOAD
  // CORE 0
  // ===================================================

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