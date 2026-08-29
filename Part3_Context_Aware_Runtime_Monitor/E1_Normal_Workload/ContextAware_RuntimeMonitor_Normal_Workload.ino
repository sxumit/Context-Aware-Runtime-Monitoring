#include <Arduino.h>
#include <math.h>
#include "esp_timer.h"

// PROJECT 1 - PART 3
// EXPERIMENT 1 - NORMAL WORKLOAD
// CONTEXT-AWARE ADAPTIVE RUNTIME MONITOR
#define LED_PIN 4

#define NUM_SAMPLES 1000

#define NORMAL_ITERATIONS 3500

#define PERIOD_MS 100
#define DEADLINE_MS 50

#define MONITOR_QUEUE_LENGTH 16

#define TASK_START_EVENT 1
#define TASK_FINISH_EVENT 2


// ADAPTIVE MONITOR SETTINGS

// Evaluate mode only once every 10 samples
#define ADAPTIVE_CHECK_INTERVAL 10

// Require 3 consecutive decisions before changing mode
#define MODE_HYSTERESIS_COUNT 3

// Execution-time thresholds in milliseconds
#define FULL_THRESHOLD_MS 25
#define BALANCED_THRESHOLD_MS 40
#define LIGHT_THRESHOLD_MS 50


// MONITOR MODES

enum MonitorMode {

  MODE_FULL = 0,
  MODE_BALANCED,
  MODE_LIGHT,
  MODE_CRITICAL
};


// MONITOR EVENT

struct MonitorEvent {

  uint8_t eventType;

  uint16_t sampleNumber;

  uint64_t timestampUs;
};


// SAMPLE RECORD

struct SampleRecord {

  unsigned long workloadTimeUs;

  unsigned long monitoredTimeUs;

  unsigned long monitoringOverheadUs;

  bool deadlineMiss;

  MonitorMode mode;

  bool detailedMonitoring;
};


SampleRecord records[NUM_SAMPLES];


// QUEUE
QueueHandle_t monitorQueue;


// BASIC STATISTICS
unsigned long deadlineMisses = 0;

unsigned long anomaliesDetected = 0;

unsigned long minWorkloadTime = ULONG_MAX;

unsigned long maxWorkloadTime = 0;

unsigned long minMonitoredTime = ULONG_MAX;

unsigned long maxMonitoredTime = 0;

double sumWorkloadTime = 0;

double sumWorkloadTimeSquared = 0;

double sumMonitoredTime = 0;

double sumMonitoredTimeSquared = 0;

double sumMonitoringOverhead = 0;


// MONITOR CPU STATISTICS
unsigned long long monitorBusyTimeUs = 0;

unsigned long monitorEventsProcessed = 0;


// ADAPTIVE STATISTICS
MonitorMode currentMode = MODE_FULL;

MonitorMode requestedMode = MODE_FULL;

unsigned int modeConfirmationCount = 0;

unsigned long modeChanges = 0;


// Number of samples processed in each mode

unsigned long fullModeSamples = 0;

unsigned long balancedModeSamples = 0;

unsigned long lightModeSamples = 0;

unsigned long criticalModeSamples = 0;


// Detailed monitoring count

unsigned long detailedMonitoringCalculations = 0;


// MONITOR STATE
uint64_t startTimestamp = 0;

uint16_t currentSample = 0;

bool taskRunning = false;

// MODE NAME
const char *modeName(
  MonitorMode mode) {

  switch (mode) {

    case MODE_FULL:
      return "FULL";

    case MODE_BALANCED:
      return "BALANCED";

    case MODE_LIGHT:
      return "LIGHT";

    case MODE_CRITICAL:
      return "CRITICAL";

    default:
      return "UNKNOWN";
  }
}

// DETERMINE DESIRED MODE
MonitorMode determineMode(
  uint64_t workloadTimeUs) {

  uint64_t timeMs =
    workloadTimeUs / 1000ULL;


  if (
    timeMs < FULL_THRESHOLD_MS) {

    return MODE_FULL;
  }


  if (
    timeMs < BALANCED_THRESHOLD_MS) {

    return MODE_BALANCED;
  }


  if (
    timeMs <= LIGHT_THRESHOLD_MS) {

    return MODE_LIGHT;
  }


  return MODE_CRITICAL;
}


// ADAPTIVE MODE UPDATE
void updateAdaptiveMode(
  uint64_t workloadTimeUs,
  uint16_t sampleNumber) {

  // Evaluate only once every 10 samples

  if (
    sampleNumber == 0 || ((sampleNumber + 1) % ADAPTIVE_CHECK_INTERVAL) != 0) {

    return;
  }


  MonitorMode newRequestedMode =
    determineMode(
      workloadTimeUs);


  // Requested mode is already current mode

  if (
    newRequestedMode == currentMode) {

    requestedMode =
      currentMode;

    modeConfirmationCount = 0;

    return;
  }


  // New requested mode

  if (
    newRequestedMode != requestedMode) {

    requestedMode =
      newRequestedMode;

    modeConfirmationCount = 1;
  }

  else {

    modeConfirmationCount++;
  }


  // Require 3 confirmations

  if (
    modeConfirmationCount >= MODE_HYSTERESIS_COUNT) {

    MonitorMode oldMode =
      currentMode;


    currentMode =
      requestedMode;


    modeConfirmationCount = 0;

    modeChanges++;


    Serial.print(
      "ADAPTIVE MODE CHANGE: ");

    Serial.print(
      modeName(oldMode));

    Serial.print(
      " -> ");

    Serial.println(
      modeName(currentMode));
  }
}

// DETAILED MONITORING DECISION
bool shouldPerformDetailedMonitoring(
  uint16_t sampleNumber) {

  switch (currentMode) {

    case MODE_FULL:

      return true;


    case MODE_BALANCED:

      return (
        sampleNumber % 2 == 0);


    case MODE_LIGHT:

      return (
        sampleNumber % 4 == 0);


    case MODE_CRITICAL:

      return (
        sampleNumber % 4 == 0);


    default:

      return false;
  }
}

// RUNTIME MONITOR
// CORE 1
void runtimeMonitorTask(
  void *parameter) {

  MonitorEvent event;


  while (true) {

    if (

      xQueueReceive(

        monitorQueue,

        &event,

        portMAX_DELAY

        )
      == pdTRUE

    ) {

      unsigned long long monitorStart =
        esp_timer_get_time();


      // START EVENT

      if (
        event.eventType == TASK_START_EVENT) {

        startTimestamp =
          event.timestampUs;

        currentSample =
          event.sampleNumber;

        taskRunning = true;
      }

      // FINISH EVENT

      else if (
        event.eventType == TASK_FINISH_EVENT) {

        if (!taskRunning) {

          continue;
        }


        uint64_t workloadTimeUs =
          event.timestampUs - startTimestamp;


        taskRunning = false;


        // SAVE WORKLOAD TIME

        records[currentSample]
          .workloadTimeUs =
          workloadTimeUs;


        // ADAPTIVE MODE UPDATE

        updateAdaptiveMode(

          workloadTimeUs,

          currentSample

        );


        // SAVE MODE

        records[currentSample]
          .mode =
          currentMode;


        // MODE SAMPLE COUNTERS
        switch (
          currentMode) {

          case MODE_FULL:

            fullModeSamples++;

            break;


          case MODE_BALANCED:

            balancedModeSamples++;

            break;


          case MODE_LIGHT:

            lightModeSamples++;

            break;


          case MODE_CRITICAL:

            criticalModeSamples++;

            break;
        }

        // DEADLINE CHECK
        bool deadlineMiss =
          workloadTimeUs > (DEADLINE_MS * 1000ULL);


        records[currentSample]
          .deadlineMiss =
          deadlineMiss;


        if (
          deadlineMiss) {

          deadlineMisses++;

          anomaliesDetected++;
        }


        // BASIC WORKLOAD STATISTICS
        if (
          workloadTimeUs < minWorkloadTime) {

          minWorkloadTime =
            workloadTimeUs;
        }


        if (
          workloadTimeUs > maxWorkloadTime) {

          maxWorkloadTime =
            workloadTimeUs;
        }


        sumWorkloadTime +=
          workloadTimeUs;


        sumWorkloadTimeSquared +=

          (double)workloadTimeUs * workloadTimeUs;


        // DETAILED MONITORING
        bool detailed =

          shouldPerformDetailedMonitoring(
            currentSample);


        records[currentSample]
          .detailedMonitoring =
          detailed;


        if (
          detailed) {

          detailedMonitoringCalculations++;
        }

        // MONITOR CPU TIME
        unsigned long long monitorFinish =
          esp_timer_get_time();


        monitorBusyTimeUs +=

          monitorFinish - monitorStart;


        monitorEventsProcessed++;
      }
    }
  }
}

// MONITORED TASK
// CORE 0

void monitoredTask(
  void *parameter) {

  TickType_t lastWakeTime =
    xTaskGetTickCount();


  // Warm-up

  vTaskDelay(
    pdMS_TO_TICKS(1000));


  for (

    int sample = 0;

    sample < NUM_SAMPLES;

    sample++

  ) {

    // TOTAL MONITORED INTERVAL START
    unsigned long long totalStart =
      esp_timer_get_time();

    // START EVENT

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


    // NORMAL WORKLOAD

    digitalWrite(
      LED_PIN,
      HIGH);


    float x = 0.5;


    for (

      int i = 0;

      i < NORMAL_ITERATIONS;

      i++

    ) {

      x =

        sin(x) * cos(x) +

        sqrt(x + 1.0);
    }


    digitalWrite(
      LED_PIN,
      LOW);


    // FINISH EVENT

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


    // TOTAL MONITORED TIME

    unsigned long long totalFinish =
      esp_timer_get_time();


    records[sample]
      .monitoredTimeUs =

      totalFinish - totalStart;


    // MAINTAIN PERIOD

    vTaskDelayUntil(

      &lastWakeTime,

      pdMS_TO_TICKS(
        PERIOD_MS)

    );
  }


  // Give monitor time to process final event

  vTaskDelay(
    pdMS_TO_TICKS(500));


  // CALCULATE OVERHEAD

  for (

    int i = 0;

    i < NUM_SAMPLES;

    i++

  ) {

    unsigned long workload =
      records[i].workloadTimeUs;


    unsigned long monitored =
      records[i].monitoredTimeUs;


    unsigned long overhead = 0;


    if (
      monitored > workload) {

      overhead =
        monitored - workload;
    }


    records[i]
      .monitoringOverheadUs =
      overhead;


    sumMonitoredTime +=
      monitored;


    sumMonitoredTimeSquared +=

      (double)monitored * monitored;


    sumMonitoringOverhead +=
      overhead;


    if (
      monitored < minMonitoredTime) {

      minMonitoredTime =
        monitored;
    }


    if (
      monitored > maxMonitoredTime) {

      maxMonitoredTime =
        monitored;
    }
  }


  // WORKLOAD STATISTICS

  double averageWorkloadTime =

    sumWorkloadTime / NUM_SAMPLES;


  double workloadVariance =
    (sumWorkloadTimeSquared / NUM_SAMPLES)
    - (averageWorkloadTime * averageWorkloadTime);


  workloadVariance =

    (sumWorkloadTimeSquared / NUM_SAMPLES)

    -

    (averageWorkloadTime * averageWorkloadTime);


  if (
    workloadVariance < 0) {

    workloadVariance = 0;
  }


  double workloadStandardDeviation =
    sqrt(
      workloadVariance);


  // MONITORED STATISTICS
  double averageMonitoredTime =

    sumMonitoredTime / NUM_SAMPLES;


  double monitoredVariance =

    (sumMonitoredTimeSquared / NUM_SAMPLES)

    -

    (averageMonitoredTime * averageMonitoredTime);


  if (
    monitoredVariance < 0) {

    monitoredVariance = 0;
  }


  double monitoredStandardDeviation =
    sqrt(
      monitoredVariance);


  // MONITORING OVERHEAD

  double averageMonitoringOverhead =

    sumMonitoringOverhead / NUM_SAMPLES;


  double overheadPercentage =

    (averageMonitoringOverhead / averageWorkloadTime)

    * 100.0;


  // CPU UTILIZATION

  double experimentTimeUs =

    NUM_SAMPLES * PERIOD_MS * 1000.0;


  double taskCpuUtilization =

    (sumWorkloadTime / experimentTimeUs)

    * 100.0;


  double monitorCpuUtilization =

    (monitorBusyTimeUs / experimentTimeUs)

    * 100.0;


  // MEMORY

  size_t freeHeap =
    ESP.getFreeHeap();


  size_t minimumFreeHeap =
    ESP.getMinFreeHeap();


  UBaseType_t stackHighWaterMark =

    uxTaskGetStackHighWaterMark(
      NULL);


  // RAW DATA

  Serial.println();

  Serial.println(
    "========================================");

  Serial.println(
    "RAW ADAPTIVE RUNTIME MONITOR DATA");

  Serial.println(
    "========================================");

  Serial.println(
    "Sample,Workload_ms,Monitored_ms,Overhead_ms,Mode,DeadlineMiss,Detailed");


  for (

    int i = 0;

    i < NUM_SAMPLES;

    i++

  ) {

    Serial.print(i + 1);

    Serial.print(",");


    Serial.print(

      records[i].workloadTimeUs / 1000.0,

      3

    );

    Serial.print(",");


    Serial.print(

      records[i].monitoredTimeUs / 1000.0,

      3

    );

    Serial.print(",");


    Serial.print(

      records[i]
          .monitoringOverheadUs
        / 1000.0,

      3

    );

    Serial.print(",");


    Serial.print(

      modeName(
        records[i].mode)

    );

    Serial.print(",");


    Serial.print(

      records[i].deadlineMiss
        ? 1
        : 0

    );

    Serial.print(",");


    Serial.println(

      records[i]
          .detailedMonitoring
        ? 1
        : 0

    );
  }


  // FINAL SUMMARY

  Serial.println();

  Serial.println(
    "========================================");

  Serial.println(
    "PROJECT 1 - PART 3");

  Serial.println(
    "EXPERIMENT 1 - NORMAL WORKLOAD");

  Serial.println(
    "CONTEXT-AWARE ADAPTIVE RUNTIME MONITOR");

  Serial.println(
    "========================================");


  Serial.print(
    "Samples: ");

  Serial.println(
    NUM_SAMPLES);


  Serial.print(
    "Normal workload iterations: ");

  Serial.println(
    NORMAL_ITERATIONS);


  Serial.print(
    "Period: ");

  Serial.print(
    PERIOD_MS);

  Serial.println(
    " ms");


  Serial.print(
    "Deadline: ");

  Serial.print(
    DEADLINE_MS);

  Serial.println(
    " ms");


  Serial.println();

  Serial.print(
    "Adaptation interval: ");

  Serial.print(
    ADAPTIVE_CHECK_INTERVAL);

  Serial.println(
    " samples");


  Serial.print(
    "Hysteresis cycles: ");

  Serial.println(
    MODE_HYSTERESIS_COUNT);


  Serial.print(
    "FULL threshold: < ");

  Serial.print(
    FULL_THRESHOLD_MS);

  Serial.println(
    " ms");


  Serial.print(
    "BALANCED threshold: < ");

  Serial.print(
    BALANCED_THRESHOLD_MS);

  Serial.println(
    " ms");


  Serial.print(
    "LIGHT threshold: <= ");

  Serial.print(
    LIGHT_THRESHOLD_MS);

  Serial.println(
    " ms");


  // WORKLOAD TIMING

  Serial.println();

  Serial.print(
    "Minimum workload execution time: ");

  Serial.print(
    minWorkloadTime / 1000.0,
    3);

  Serial.println(
    " ms");


  Serial.print(
    "Maximum workload execution time: ");

  Serial.print(
    maxWorkloadTime / 1000.0,
    3);

  Serial.println(
    " ms");


  Serial.print(
    "Average workload execution time: ");

  Serial.print(
    averageWorkloadTime / 1000.0,
    3);

  Serial.println(
    " ms");


  Serial.print(
    "Workload timing standard deviation: ");

  Serial.print(
    workloadStandardDeviation / 1000.0,
    3);

  Serial.println(
    " ms");


  // MONITORED TIMING

  Serial.println();

  Serial.print(
    "Minimum monitored execution time: ");

  Serial.print(
    minMonitoredTime / 1000.0,
    3);

  Serial.println(
    " ms");


  Serial.print(
    "Maximum monitored execution time: ");

  Serial.print(
    maxMonitoredTime / 1000.0,
    3);

  Serial.println(
    " ms");


  Serial.print(
    "Average monitored execution time: ");

  Serial.print(
    averageMonitoredTime / 1000.0,
    3);

  Serial.println(
    " ms");


  Serial.print(
    "Monitored timing standard deviation: ");

  Serial.print(
    monitoredStandardDeviation / 1000.0,
    3);

  Serial.println(
    " ms");


  // ADAPTIVE MODE STATISTICS

  Serial.println();

  Serial.print(
    "FULL-mode monitored samples: ");

  Serial.println(
    fullModeSamples);


  Serial.print(
    "BALANCED-mode monitored samples: ");

  Serial.println(
    balancedModeSamples);


  Serial.print(
    "LIGHT-mode monitored samples: ");

  Serial.println(
    lightModeSamples);


  Serial.print(
    "CRITICAL-mode monitored samples: ");

  Serial.println(
    criticalModeSamples);


  Serial.print(
    "Mode changes: ");

  Serial.println(
    modeChanges);


  Serial.print(
    "Detailed monitoring calculations: ");

  Serial.println(
    detailedMonitoringCalculations);


  // COVERAGE
  double monitoringCoverage =

    ((double)monitorEventsProcessed /

     (NUM_SAMPLES * 2.0))

    * 100.0;


  Serial.println();

  Serial.print(
    "Monitoring coverage: ");

  Serial.print(
    monitoringCoverage,
    2);

  Serial.println(
    " %");


  Serial.print(
    "Monitor events processed: ");

  Serial.println(
    monitorEventsProcessed);

  // DEADLINE DETECTION
  double deadlineAnomalyRate =

    ((double)anomaliesDetected / NUM_SAMPLES)

    * 100.0;


  Serial.println();

  Serial.print(
    "Detected deadline anomalies: ");

  Serial.println(
    anomaliesDetected);


  Serial.print(
    "Deadline misses: ");

  Serial.println(
    deadlineMisses);


  Serial.print(
    "Deadline anomaly rate: ");

  Serial.print(
    deadlineAnomalyRate,
    2);

  Serial.println(
    " %");

  // OVERHEAD
  Serial.println();

  Serial.print(
    "Average monitoring overhead: ");

  Serial.print(
    averageMonitoringOverhead / 1000.0,
    3);

  Serial.println(
    " ms");


  Serial.print(
    "Monitoring overhead percentage: ");

  Serial.print(
    overheadPercentage,
    3);

  Serial.println(
    " %");

  // CPU
  Serial.println();

  Serial.print(
    "Monitored task CPU utilization: ");

  Serial.print(
    taskCpuUtilization,
    2);

  Serial.println(
    " %");


  Serial.print(
    "Runtime monitor CPU utilization: ");

  Serial.print(
    monitorCpuUtilization,
    3);

  Serial.println(
    " %");


  // MEMORY

  Serial.println();

  Serial.print(
    "Free heap: ");

  Serial.print(
    freeHeap);

  Serial.println(
    " bytes");


  Serial.print(
    "Minimum free heap: ");

  Serial.print(
    minimumFreeHeap);

  Serial.println(
    " bytes");


  Serial.print(
    "Monitor task stack high-water mark: ");

  Serial.print(
    stackHighWaterMark);

  Serial.println(
    " words");


  Serial.println();

  Serial.println(
    "Part 3 Experiment 1 complete.");

  Serial.println(
    "========================================");


  vTaskDelete(
    NULL);
}


// SETUP
void setup() {

  Serial.begin(
    115200);


  pinMode(
    LED_PIN,
    OUTPUT);


  digitalWrite(
    LED_PIN,
    LOW);

  // CREATE MONITOR QUEUE
  monitorQueue =

    xQueueCreate(

      MONITOR_QUEUE_LENGTH,

      sizeof(MonitorEvent)

    );


  if (
    monitorQueue == NULL) {

    Serial.println(
      "ERROR: Monitor queue creation failed.");


    while (true) {

      delay(1000);
    }
  }

  // ADAPTIVE MONITOR -> CORE 1
  xTaskCreatePinnedToCore(

    runtimeMonitorTask,

    "AdaptiveMonitor",

    4096,

    NULL,

    2,

    NULL,

    1

  );


  // MONITORED TASK -> CORE 0
  xTaskCreatePinnedToCore(

    monitoredTask,

    "MonitoredTask",

    4096,

    NULL,

    1,

    NULL,

    0

  );
}


void loop() {
}
