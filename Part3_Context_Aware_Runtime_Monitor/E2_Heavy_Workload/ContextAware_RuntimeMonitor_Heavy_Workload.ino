
#include <Arduino.h>
#include <math.h>
#include "esp_timer.h"
// PROJECT 1 - PART 3
// EXPERIMENT 2 - HEAVY WORKLOAD
// ADAPTIVE RUNTIME MONITOR

#define LED_PIN 4

#define NUM_SAMPLES 1000
#define HEAVY_ITERATIONS 10000

#define PERIOD_MS 100
#define DEADLINE_MS 50

#define MONITOR_QUEUE_LENGTH 16

#define TASK_START_EVENT  1
#define TASK_FINISH_EVENT 2

// ADAPTIVE MONITOR CONFIGURATION

// Mode decision is evaluated only once every 10 completed samples.
#define ADAPTATION_INTERVAL 10

// Three consecutive decisions are required before
// changing the monitoring mode.
#define HYSTERESIS_CYCLES 3

// EXECUTION-TIME THRESHOLDS
//
// < 25 ms       -> FULL
// 25-40 ms      -> BALANCED
// 40-50 ms      -> LIGHT
// > 50 ms       -> CRITICAL

#define FULL_LIMIT_US      25000ULL
#define BALANCED_LIMIT_US  40000ULL
#define LIGHT_LIMIT_US     50000ULL


// MONITOR MODES

enum MonitorMode {

  MODE_FULL,

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
};


SampleRecord records[NUM_SAMPLES];


// QUEUE

QueueHandle_t monitorQueue;

// STATISTICS

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


unsigned long fullSamples = 0;

unsigned long balancedSamples = 0;

unsigned long lightSamples = 0;

unsigned long criticalSamples = 0;

unsigned long modeChanges = 0;


// MONITOR STATE

uint64_t startTimestamp = 0;

uint16_t currentSample = 0;

bool taskRunning = false;

// ADAPTIVE STATE

MonitorMode currentMode = MODE_FULL;

uint8_t increaseCounter = 0;

uint8_t decreaseCounter = 0;



// MODE NAME


const char* modeName(
  MonitorMode mode
) {

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


// DETERMINE TARGET MODE

MonitorMode determineTargetMode(
  uint64_t executionTimeUs
) {

  if (
    executionTimeUs <
    FULL_LIMIT_US
  ) {

    return MODE_FULL;
  }


  if (
    executionTimeUs <
    BALANCED_LIMIT_US
  ) {

    return MODE_BALANCED;
  }


  if (
    executionTimeUs <=
    LIGHT_LIMIT_US
  ) {

    return MODE_LIGHT;
  }


  return MODE_CRITICAL;
}


// ADAPTIVE MODE CONTROLLER
// Called only once every 10 samples.
// Three consecutive decisions are required before
// changing mode.
void updateAdaptiveMode(
  uint64_t executionTimeUs
) {

  MonitorMode targetMode =
    determineTargetMode(
      executionTimeUs
    );


  
  // No mode change required
 

  if (
    targetMode ==
    currentMode
  ) {

    increaseCounter = 0;

    decreaseCounter = 0;

    return;
  }


  
  // Need MORE monitoring
 
  if (
    targetMode <
    currentMode
  ) {

    increaseCounter++;

    decreaseCounter = 0;


    if (
      increaseCounter >=
      HYSTERESIS_CYCLES
    ) {

      MonitorMode oldMode =
        currentMode;


      currentMode =
        targetMode;


      increaseCounter = 0;


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

    return;
  }

  // Can use LESS monitoring

  decreaseCounter++;

  increaseCounter = 0;


  if (
    decreaseCounter >=
    HYSTERESIS_CYCLES
  ) {

    MonitorMode oldMode =
      currentMode;


    currentMode =
      targetMode;


    decreaseCounter = 0;


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

// RUNTIME MONITOR
// CORE 1

void runtimeMonitorTask(
  void *parameter
) {

  MonitorEvent event;


  while (true) {

    if (
      xQueueReceive(
        monitorQueue,
        &event,
        portMAX_DELAY
      ) != pdTRUE
    ) {

      continue;
    }


    unsigned long long monitorStart =
      esp_timer_get_time();

    // START EVENT
    
    if (
      event.eventType ==
      TASK_START_EVENT
    ) {

      startTimestamp =
        event.timestampUs;


      currentSample =
        event.sampleNumber;


      taskRunning =
        true;
    }

    // FINISH EVENT

    else if (
      event.eventType ==
      TASK_FINISH_EVENT
    ) {

      if (
        !taskRunning
      ) {

        continue;
      }

      // Calculate workload execution time

      uint64_t workloadTimeUs =
        event.timestampUs -
        startTimestamp;


      taskRunning =
        false;

      // Store raw workload timing
      records[currentSample]
        .workloadTimeUs =
        workloadTimeUs;


      // DEADLINE CHECK


      bool deadlineMiss =
        workloadTimeUs >
        (DEADLINE_MS * 1000ULL);


      records[currentSample]
        .deadlineMiss =
        deadlineMiss;


      if (
        deadlineMiss
      ) {

        deadlineMisses++;

        anomaliesDetected++;
      }


      // ADAPTIVE DECISION
      // Only every 10th sample.

      if (
        (
          (currentSample + 1) %
          ADAPTATION_INTERVAL
        ) == 0
      ) {

        updateAdaptiveMode(
          workloadTimeUs
        );
      }


      // SAVE CURRENT MODE

      records[currentSample]
        .mode =
        currentMode;


      // MODE COUNTERS


      switch (
        currentMode
      ) {

        case MODE_FULL:

          fullSamples++;

          break;


        case MODE_BALANCED:

          balancedSamples++;

          break;


        case MODE_LIGHT:

          lightSamples++;

          break;


        case MODE_CRITICAL:

          criticalSamples++;

          break;
      }


      // WORKLOAD STATISTICS

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

    // MONITOR CPU TIME

    unsigned long long monitorFinish =
      esp_timer_get_time();


    monitorBusyTimeUs +=
      monitorFinish -
      monitorStart;


    monitorEventsProcessed++;
  }
}
// HEAVY MONITORED TASK
// CORE 0

void monitoredTask(
  void *parameter
) {

  TickType_t lastWakeTime =
    xTaskGetTickCount();


  // WARM-UP\

  vTaskDelay(
    pdMS_TO_TICKS(1000)
  );


  // EXPERIMENT

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


    // HEAVY WORKLOAD

    digitalWrite(
      LED_PIN,
      HIGH
    );


    float x =
      0.5;


    for (
      int i = 0;
      i < HEAVY_ITERATIONS;
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


    // TOTAL MONITORED INTERVAL END
    unsigned long long totalFinish =
      esp_timer_get_time();


    records[sample]
      .monitoredTimeUs =
      totalFinish -
      totalStart;


    // MAINTAIN 100 ms PERIOD
    vTaskDelayUntil(
      &lastWakeTime,
      pdMS_TO_TICKS(PERIOD_MS)
    );
  }

  // ALLOW MONITOR TO FINISH

  vTaskDelay(
    pdMS_TO_TICKS(500)
  );


  // CALCULATE MONITORED STATISTICS

  for (
    int i = 0;
    i < NUM_SAMPLES;
    i++
  ) {

    unsigned long workload =
      records[i].workloadTimeUs;


    unsigned long monitored =
      records[i].monitoredTimeUs;


    unsigned long overhead =
      0;


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


    if (
      monitored <
      minMonitoredTime
    ) {

      minMonitoredTime =
        monitored;
    }


    if (
      monitored >
      maxMonitoredTime
    ) {

      maxMonitoredTime =
        monitored;
    }
  }


  // WORKLOAD STATISTICS

  double averageWorkloadTime =
    sumWorkloadTime /
    NUM_SAMPLES;


  double workloadVariance =
    (
      sumWorkloadTimeSquared /
      NUM_SAMPLES
    )
    -
    (
      averageWorkloadTime *
      averageWorkloadTime
    );


  if (
    workloadVariance < 0
  ) {

    workloadVariance =
      0;
  }


  double workloadStandardDeviation =
    sqrt(
      workloadVariance
    );


  // MONITORED STATISTICS

  double averageMonitoredTime =
    sumMonitoredTime /
    NUM_SAMPLES;


  double monitoredVariance =
    (
      sumMonitoredTimeSquared /
      NUM_SAMPLES
    )
    -
    (
      averageMonitoredTime *
      averageMonitoredTime
    );


  if (
    monitoredVariance < 0
  ) {

    monitoredVariance =
      0;
  }


  double monitoredStandardDeviation =
    sqrt(
      monitoredVariance
    );


  // MONITORING OVERHEAD

  double averageMonitoringOverhead =
    sumMonitoringOverhead /
    NUM_SAMPLES;


  double overheadPercentage =
    (
      averageMonitoringOverhead /
      averageWorkloadTime
    ) * 100.0;

  // CPU UTILIZATION
  double experimentTimeUs =
    NUM_SAMPLES *
    PERIOD_MS *
    1000.0;


  double taskCpuUtilization =
    (
      sumWorkloadTime /
      experimentTimeUs
    ) * 100.0;


  double monitorCpuUtilization =
    (
      monitorBusyTimeUs /
      experimentTimeUs
    ) * 100.0;


  // MEMORY

  size_t freeHeap =
    ESP.getFreeHeap();


  size_t minimumFreeHeap =
    ESP.getMinFreeHeap();


  UBaseType_t stackHighWaterMark =
    uxTaskGetStackHighWaterMark(
      NULL
    );


  // RAW DATA

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "RAW ADAPTIVE RUNTIME MONITOR DATA"
  );

  Serial.println(
    "========================================"
  );


  Serial.println(
    "Sample,Workload_ms,Monitored_ms,Overhead_ms,Mode,DeadlineMiss"
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


    Serial.print(
      modeName(
        records[i].mode
      )
    );

    Serial.print(",");


    Serial.println(
      records[i].deadlineMiss
      ? 1
      : 0
    );
  }


  // FINAL SUMMARY

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "PROJECT 1 - PART 2"
  );

  Serial.println(
    "EXPERIMENT 2 - HEAVY WORKLOAD"
  );

  Serial.println(
    "ADAPTIVE RUNTIME MONITOR"
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
    "Heavy workload iterations: "
  );

  Serial.println(
    HEAVY_ITERATIONS
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


  // ADAPTIVE CONFIGURATION

  Serial.println();

  Serial.print(
    "Adaptation interval: "
  );

  Serial.print(
    ADAPTATION_INTERVAL
  );

  Serial.println(
    " samples"
  );


  Serial.print(
    "Hysteresis cycles: "
  );

  Serial.println(
    HYSTERESIS_CYCLES
  );


  Serial.print(
    "FULL threshold: < "
  );

  Serial.print(
    FULL_LIMIT_US / 1000
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "BALANCED threshold: < "
  );

  Serial.print(
    BALANCED_LIMIT_US / 1000
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "LIGHT threshold: <= "
  );

  Serial.print(
    LIGHT_LIMIT_US / 1000
  );

  Serial.println(
    " ms"
  );


  // WORKLOAD TIMING

  Serial.println();

  Serial.print(
    "Minimum workload execution time: "
  );

  Serial.print(
    minWorkloadTime / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Maximum workload execution time: "
  );

  Serial.print(
    maxWorkloadTime / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Average workload execution time: "
  );

  Serial.print(
    averageWorkloadTime / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Workload timing standard deviation: "
  );

  Serial.print(
    workloadStandardDeviation / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  // MONITORED TIMING

  Serial.println();

  Serial.print(
    "Minimum monitored execution time: "
  );

  Serial.print(
    minMonitoredTime / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Maximum monitored execution time: "
  );

  Serial.print(
    maxMonitoredTime / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Average monitored execution time: "
  );

  Serial.print(
    averageMonitoredTime / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Monitored timing standard deviation: "
  );

  Serial.print(
    monitoredStandardDeviation / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  // MODE DISTRIBUTION
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
    "Mode changes: "
  );

  Serial.println(
    modeChanges
  );


  // COVERAGE

  Serial.println();

  Serial.println(
    "Monitoring coverage: 100.00 %"
  );


  // OVERHEAD

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


  // DEADLINE DETECTION

  Serial.println();

  Serial.print(
    "Detected deadline anomalies: "
  );

  Serial.println(
    anomaliesDetected
  );


  Serial.print(
    "Deadline misses: "
  );

  Serial.println(
    deadlineMisses
  );


  Serial.print(
    "Deadline anomaly rate: "
  );

  Serial.print(
    (
      (double)anomaliesDetected /
      NUM_SAMPLES
    ) * 100.0,
    2
  );

  Serial.println(
    " %"
  );


  // CPU

  Serial.println();

  Serial.print(
    "Monitored task CPU utilization: "
  );

  Serial.print(
    taskCpuUtilization,
    2
  );

  Serial.println(
    " %"
  );


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


  // MEMORY

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
    "Monitored task stack high-water mark: "
  );

  Serial.print(
    stackHighWaterMark
  );

  Serial.println(
    " words"
  );


  Serial.println();

  Serial.println(
    "Part 2 Experiment 2 complete."
  );

  Serial.println(
    "========================================"
  );


  vTaskDelete(
    NULL
  );
}


// SETUP

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


  // CREATE MONITOR QUEUE

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


  // RUNTIME MONITOR -> CORE 1

  xTaskCreatePinnedToCore(

    runtimeMonitorTask,

    "RuntimeMonitor",

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

