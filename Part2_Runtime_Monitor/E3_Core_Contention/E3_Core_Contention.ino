#include <Arduino.h>
#include <math.h>
#include "esp_timer.h"

// PROJECT 1 - PART 2
// EXPERIMENT 3 - CORE CONTENTION
// RUNTIME MONITOR

#define LED_PIN 4

#define NUM_SAMPLES 1000

#define NORMAL_ITERATIONS 4500
#define CONTENDER_ITERATIONS 5000

#define CONTENDER_DELAY_MS 10

#define PERIOD_MS 100
#define DEADLINE_MS 50

#define MONITOR_QUEUE_LENGTH 16

#define TASK_START_EVENT  1
#define TASK_FINISH_EVENT 2


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


// MONITOR STATE

uint64_t startTimestamp = 0;
uint16_t currentSample = 0;
bool taskRunning = false;


// RUNTIME MONITOR
// CORE 1

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


      // START EVENT
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


      // FINISH EVENT

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


        if (deadlineMiss) {

          deadlineMisses++;
          anomaliesDetected++;
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


      unsigned long long monitorFinish =
        esp_timer_get_time();


      monitorBusyTimeUs +=
        monitorFinish -
        monitorStart;


      monitorEventsProcessed++;
    }
  }
}


// NORMAL MONITORED TASK
// CORE 0

void monitoredTask(void *parameter) {

  TickType_t lastWakeTime =
    xTaskGetTickCount();


  // Warm-up
  vTaskDelay(
    pdMS_TO_TICKS(1000)
  );


  for (
    int sample = 0;
    sample < NUM_SAMPLES;
    sample++
  ) {

    // TOTAL MONITORED INTERVAL START
    unsigned long long totalStart =
      esp_timer_get_time();


    // SEND START EVENT

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
      HIGH
    );


    float x = 0.5;


    for (
      int i = 0;
      i < NORMAL_ITERATIONS;
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


    // SEND FINISH EVENT

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
      totalFinish -
      totalStart;


    // MAINTAIN 100 ms PERIOD

    vTaskDelayUntil(
      &lastWakeTime,
      pdMS_TO_TICKS(PERIOD_MS)
    );
  }


  // Give monitor time to process final event
  vTaskDelay(
    pdMS_TO_TICKS(500)
  );


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
      monitored > workload
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


  if (workloadVariance < 0)
    workloadVariance = 0;


  double workloadStandardDeviation =
    sqrt(workloadVariance);


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


  if (monitoredVariance < 0)
    monitoredVariance = 0;


  double monitoredStandardDeviation =
    sqrt(monitoredVariance);


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
    uxTaskGetStackHighWaterMark(NULL);


  // RAW DATA

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "RAW RUNTIME MONITOR DATA"
  );

  Serial.println(
    "========================================"
  );

  Serial.println(
    "Run,Workload_ms,Monitored_ms,Overhead_ms,Deadline_Miss"
  );


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
      records[i].monitoringOverheadUs / 1000.0,
      3
    );

    Serial.print(",");

    Serial.println(
      records[i].deadlineMiss ? 1 : 0
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
    "EXPERIMENT 3 - CORE CONTENTION"
  );

  Serial.println(
    "RUNTIME MONITOR"
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
    "Baseline iterations: "
  );

  Serial.println(
    NORMAL_ITERATIONS
  );


  Serial.print(
    "Contender iterations: "
  );

  Serial.println(
    CONTENDER_ITERATIONS
  );


  Serial.print(
    "Contender delay: "
  );

  Serial.print(
    CONTENDER_DELAY_MS
  );

  Serial.println(
    " ms"
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


  // ---------------------------------------------------
  // WORKLOAD TIMING
  // ---------------------------------------------------

  Serial.println();

  Serial.print(
    "Minimum workload execution time: "
  );

  Serial.print(
    minWorkloadTime / 1000.0,
    3
  );

  Serial.println(" ms");


  Serial.print(
    "Maximum workload execution time: "
  );

  Serial.print(
    maxWorkloadTime / 1000.0,
    3
  );

  Serial.println(" ms");


  Serial.print(
    "Average workload execution time: "
  );

  Serial.print(
    averageWorkloadTime / 1000.0,
    3
  );

  Serial.println(" ms");


  Serial.print(
    "Workload timing standard deviation: "
  );

  Serial.print(
    workloadStandardDeviation / 1000.0,
    3
  );

  Serial.println(" ms");


  // MONITORED TIMING

  Serial.println();

  Serial.print(
    "Minimum monitored execution time: "
  );

  Serial.print(
    minMonitoredTime / 1000.0,
    3
  );

  Serial.println(" ms");


  Serial.print(
    "Maximum monitored execution time: "
  );

  Serial.print(
    maxMonitoredTime / 1000.0,
    3
  );

  Serial.println(" ms");


  Serial.print(
    "Average monitored execution time: "
  );

  Serial.print(
    averageMonitoredTime / 1000.0,
    3
  );

  Serial.println(" ms");


  Serial.print(
    "Monitored timing standard deviation: "
  );

  Serial.print(
    monitoredStandardDeviation / 1000.0,
    3
  );

  Serial.println(" ms");


  // OVERHEAD

  Serial.println();

  Serial.print(
    "Average monitoring overhead: "
  );

  Serial.print(
    averageMonitoringOverhead / 1000.0,
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


  // CPU

  Serial.println();

  Serial.print(
    "Monitored task CPU utilization: "
  );

  Serial.print(
    taskCpuUtilization,
    2
  );

  Serial.println(" %");


  Serial.print(
    "Runtime monitor CPU utilization: "
  );

  Serial.print(
    monitorCpuUtilization,
    3
  );

  Serial.println(" %");


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

  Serial.println(" bytes");


  Serial.print(
    "Minimum free heap: "
  );

  Serial.print(
    minimumFreeHeap
  );

  Serial.println(" bytes");


  Serial.print(
    "Monitored task stack high-water mark: "
  );

  Serial.print(
    stackHighWaterMark
  );

  Serial.println(" words");


  Serial.println();

  Serial.println(
    "Part 2 Experiment 3 complete."
  );

  Serial.println(
    "========================================"
  );


  vTaskDelete(NULL);
}


// CPU CONTENDER
// CORE 0

void contenderTask(void *parameter) {

  // Small delay so the normal task can start
  vTaskDelay(
    pdMS_TO_TICKS(1000)
  );


  while (true) {

    float x = 0.7;


    // CONTROLLED CPU WORK

    for (
      int i = 0;
      i < CONTENDER_ITERATIONS;
      i++
    ) {

      x =
        sin(x) *
        cos(x) +
        sqrt(x + 1.0);
    }


    // Actually sleep and give Core 0 time to the
    // normal task.

    vTaskDelay(
      pdMS_TO_TICKS(
        CONTENDER_DELAY_MS
      )
    );
  }
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


  // RUNTIME MONITOR → CORE 1
  // Priority 2
  xTaskCreatePinnedToCore(

    runtimeMonitorTask,

    "RuntimeMonitor",

    4096,

    NULL,

    2,

    NULL,

    1
  );


  // NORMAL TASK → CORE 0
  // Priority 1
  xTaskCreatePinnedToCore(

    monitoredTask,

    "MonitoredTask",

    4096,

    NULL,

    1,

    NULL,

    0
  );


  // CONTENDER → CORE 0
  // Priority 1

  xTaskCreatePinnedToCore(

    contenderTask,

    "CPUContender",

    4096,

    NULL,

    1,

    NULL,

    0
  );
}


void loop() {

}
