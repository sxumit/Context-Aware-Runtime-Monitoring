#include <math.h>
#include "esp_timer.h"

#define LED_PIN 4

#define PERIOD_MS 100
#define DEADLINE_MS 50

#define NUM_SAMPLES 1000

// Measured task: same workload as E1
#define BASELINE_ITERATIONS 3500

// Competing task workload
#define CONTENDER_ITERATIONS 5000

// Contender sleeps after each workload
#define CONTENDER_DELAY_MS 10


// =====================================================
// RAW EXECUTION-TIME STORAGE
// =====================================================

unsigned long executionTimes[NUM_SAMPLES];


// =====================================================
// RESULTS / STATISTICS
// =====================================================

volatile float baselineResult = 0.0;
volatile float contenderResult = 0.0;

unsigned long long totalExecutionTime = 0;
unsigned long long totalBusyTime = 0;

unsigned long minExecutionTime = ULONG_MAX;
unsigned long maxExecutionTime = 0;

unsigned long deadlineMisses = 0;

double sumExecutionTime = 0;
double sumExecutionTimeSquared = 0;


// =====================================================
// CONTENDER TASK
// =====================================================

void contenderTask(void *parameter) {

  float x = 0.7;

  while (true) {

    // CPU-intensive workload
    for (int i = 0;
         i < CONTENDER_ITERATIONS;
         i++) {

      x = sin(x) * cos(x) +
          sqrt(x + 1.0);
    }

    contenderResult = x;

    // IMPORTANT:
    // Actually sleep instead of continuously
    // demanding the CPU.
    vTaskDelay(
      pdMS_TO_TICKS(CONTENDER_DELAY_MS)
    );
  }
}


// =====================================================
// BASELINE / MEASURED TASK
// =====================================================

void baselineTask(void *parameter) {

  TickType_t lastWakeTime =
    xTaskGetTickCount();


  // Warm-up
  vTaskDelay(
    pdMS_TO_TICKS(1000)
  );


  // ===================================================
  // 1000 MEASUREMENTS
  // ===================================================

  for (int sample = 0;
       sample < NUM_SAMPLES;
       sample++) {


    // -----------------------------------------------
    // START TIME
    // -----------------------------------------------

    unsigned long long startTime =
      esp_timer_get_time();

    digitalWrite(
      LED_PIN,
      HIGH
    );


    // -----------------------------------------------
    // SAME NORMAL WORKLOAD AS E1
    // -----------------------------------------------

    float x = 0.5;

    for (int i = 0;
         i < BASELINE_ITERATIONS;
         i++) {

      x = sin(x) * cos(x) +
          sqrt(x + 1.0);
    }

    baselineResult = x;


    // -----------------------------------------------
    // FINISH TIME
    // -----------------------------------------------

    unsigned long long finishTime =
      esp_timer_get_time();

    digitalWrite(
      LED_PIN,
      LOW
    );


    // Execution time in microseconds
    unsigned long executionTime =
      finishTime - startTime;


    // -----------------------------------------------
    // SAVE RAW SAMPLE
    // -----------------------------------------------

    executionTimes[sample] =
      executionTime;


    // -----------------------------------------------
    // UPDATE STATISTICS
    // -----------------------------------------------

    totalExecutionTime +=
      executionTime;

    totalBusyTime +=
      executionTime;


    if (executionTime <
        minExecutionTime) {

      minExecutionTime =
        executionTime;
    }


    if (executionTime >
        maxExecutionTime) {

      maxExecutionTime =
        executionTime;
    }


    sumExecutionTime +=
      executionTime;

    sumExecutionTimeSquared +=
      (double)executionTime *
      executionTime;


    // -----------------------------------------------
    // DEADLINE CHECK
    // -----------------------------------------------

    if (executionTime >
        (DEADLINE_MS * 1000ULL)) {

      deadlineMisses++;
    }


    // -----------------------------------------------
    // MAINTAIN 100 ms PERIOD
    // -----------------------------------------------

    vTaskDelayUntil(
      &lastWakeTime,
      pdMS_TO_TICKS(PERIOD_MS)
    );
  }


  // ===================================================
  // FINAL STATISTICS
  // ===================================================

  double averageExecutionTime =
    sumExecutionTime /
    NUM_SAMPLES;


  double variance =
    (sumExecutionTimeSquared /
     NUM_SAMPLES)
    -
    (averageExecutionTime *
     averageExecutionTime);


  double standardDeviation =
    sqrt(variance);


  double experimentTime =
    NUM_SAMPLES *
    PERIOD_MS *
    1000.0;


  double cpuUtilization =
    (totalBusyTime /
     experimentTime) *
    100.0;


  // ===================================================
  // MEMORY
  // ===================================================

  size_t freeHeap =
    ESP.getFreeHeap();

  size_t minimumFreeHeap =
    ESP.getMinFreeHeap();


  UBaseType_t baselineStack =
    uxTaskGetStackHighWaterMark(
      NULL
    );


  // ===================================================
  // RAW DATA
  // ===================================================

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "RAW EXECUTION-TIME DATA"
  );

  Serial.println(
    "========================================"
  );

  Serial.println(
    "Run,Execution_Time_us"
  );


  for (int i = 0;
       i < NUM_SAMPLES;
       i++) {

    Serial.print(i + 1);

    Serial.print(",");

    Serial.println(
      executionTimes[i]
    );
  }


  // ===================================================
  // FINAL SUMMARY
  // ===================================================

  Serial.println();

  Serial.println(
    "========================================"
  );

  Serial.println(
    "PROJECT 1 - PART 1 BASELINE"
  );

  Serial.println(
    "EXPERIMENT 3 - CORE CONTENTION"
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
    BASELINE_ITERATIONS
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


  Serial.println();


  Serial.print(
    "Minimum execution time: "
  );

  Serial.print(
    minExecutionTime / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Maximum execution time: "
  );

  Serial.print(
    maxExecutionTime / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Average execution time: "
  );

  Serial.print(
    averageExecutionTime / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Timing standard deviation: "
  );

  Serial.print(
    standardDeviation / 1000.0,
    3
  );

  Serial.println(
    " ms"
  );


  Serial.print(
    "Deadline misses: "
  );

  Serial.println(
    deadlineMisses
  );


  Serial.print(
    "Deadline miss rate: "
  );

  Serial.print(
    (deadlineMisses * 100.0) /
    NUM_SAMPLES,
    2
  );

  Serial.println(
    " %"
  );


  Serial.print(
    "Baseline task CPU utilization: "
  );

  Serial.print(
    cpuUtilization,
    2
  );

  Serial.println(
    " %"
  );


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
    "Baseline stack high-water mark: "
  );

  Serial.print(
    baselineStack
  );

  Serial.println(
    " words"
  );


  Serial.println();


  Serial.println(
    "Baseline Experiment 3 complete."
  );


  Serial.println(
    "========================================"
  );


  // Stop measured task
  vTaskDelete(
    NULL
  );
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


  // ---------------------------------------------------
  // CONTENDER → CORE 0
  // ---------------------------------------------------

  xTaskCreatePinnedToCore(

    contenderTask,

    "ContenderTask",

    4096,

    NULL,

    1,

    NULL,

    0
  );


  // ---------------------------------------------------
  // MEASURED TASK → CORE 0
  // ---------------------------------------------------

  xTaskCreatePinnedToCore(

    baselineTask,

    "BaselineTask",

    4096,

    NULL,

    1,

    NULL,

    0
  );
}


void loop() {

}