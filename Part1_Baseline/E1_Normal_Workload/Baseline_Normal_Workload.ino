#include <math.h>
#include "esp_timer.h"

#define LED_PIN 4

#define PERIOD_MS 100
#define DEADLINE_MS 50

#define NUM_SAMPLES 1000

// Calibrated normal workload
#define NORMAL_ITERATIONS 3500

volatile float calculationResult = 0.0;

unsigned long executionTimes[NUM_SAMPLES];

unsigned long long totalExecutionTime = 0;
unsigned long long totalBusyTime = 0;

unsigned long minExecutionTime = ULONG_MAX;
unsigned long maxExecutionTime = 0;

unsigned long deadlineMisses = 0;

double sumExecutionTime = 0;
double sumExecutionTimeSquared = 0;


void baselineTask(void *parameter) {

  TickType_t lastWakeTime = xTaskGetTickCount();

  // Warm-up
  vTaskDelay(pdMS_TO_TICKS(1000));

  for (int sample = 0; sample < NUM_SAMPLES; sample++) {

    // START TIME
    unsigned long long startTime =
      esp_timer_get_time();

    digitalWrite(LED_PIN, HIGH);

    // NORMAL CPU WORKLOAD
    float x = 0.5;

    for (int i = 0; i < NORMAL_ITERATIONS; i++) {

      x = sin(x) * cos(x) + sqrt(x + 1.0);

    }

    calculationResult = x;


    // FINISH TIME
    unsigned long long finishTime =
      esp_timer_get_time();

    digitalWrite(LED_PIN, LOW);


    // Execution time in microseconds
    unsigned long executionTime =
      finishTime - startTime;

    // STORE RAW MEASUREMENT
    executionTimes[sample] =
      executionTime;

    // STATISTICS
    totalExecutionTime += executionTime;
    totalBusyTime += executionTime;


    if (executionTime < minExecutionTime) {
      minExecutionTime = executionTime;
    }


    if (executionTime > maxExecutionTime) {
      maxExecutionTime = executionTime;
    }


    sumExecutionTime += executionTime;

    sumExecutionTimeSquared +=
      (double)executionTime *
      executionTime;

    // DEADLINE CHECK
    if (executionTime >
        (DEADLINE_MS * 1000ULL)) {

      deadlineMisses++;

    }

    // MAINTAIN PERIOD
    vTaskDelayUntil(
      &lastWakeTime,
      pdMS_TO_TICKS(PERIOD_MS)
    );
  }

  // FINAL STATISTICS
  double averageExecutionTime =
    sumExecutionTime / NUM_SAMPLES;


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

  // MEMORY
  size_t freeHeap =
    ESP.getFreeHeap();

  size_t minimumFreeHeap =
    ESP.getMinFreeHeap();

  UBaseType_t stackHighWaterMark =
    uxTaskGetStackHighWaterMark(NULL);

  // RAW DATA
  Serial.println();
  Serial.println("========================================");
  Serial.println("RAW EXECUTION-TIME DATA");
  Serial.println("========================================");

  Serial.println("Run,Execution_Time_us");

  for (int i = 0; i < NUM_SAMPLES; i++) {

    Serial.print(i + 1);
    Serial.print(",");
    Serial.println(executionTimes[i]);

  }

  // FINAL SUMMARY
  Serial.println();
  Serial.println("========================================");
  Serial.println("PROJECT 1 - PART 1 BASELINE");
  Serial.println("EXPERIMENT 1 - NORMAL WORKLOAD");
  Serial.println("========================================");

  Serial.print("Samples: ");
  Serial.println(NUM_SAMPLES);

  Serial.print("Normal iterations: ");
  Serial.println(NORMAL_ITERATIONS);

  Serial.print("Period: ");
  Serial.print(PERIOD_MS);
  Serial.println(" ms");

  Serial.print("Deadline: ");
  Serial.print(DEADLINE_MS);
  Serial.println(" ms");

  Serial.println();

  Serial.print("Minimum execution time: ");
  Serial.print(
    minExecutionTime / 1000.0,
    3
  );
  Serial.println(" ms");

  Serial.print("Maximum execution time: ");
  Serial.print(
    maxExecutionTime / 1000.0,
    3
  );
  Serial.println(" ms");

  Serial.print("Average execution time: ");
  Serial.print(
    averageExecutionTime / 1000.0,
    3
  );
  Serial.println(" ms");

  Serial.print("Timing standard deviation: ");
  Serial.print(
    standardDeviation / 1000.0,
    3
  );
  Serial.println(" ms");

  Serial.print("Deadline misses: ");
  Serial.println(deadlineMisses);

  Serial.print("Deadline miss rate: ");
  Serial.print(
    (deadlineMisses * 100.0) /
    NUM_SAMPLES,
    2
  );
  Serial.println(" %");

  Serial.print("Task CPU utilization: ");
  Serial.print(cpuUtilization, 2);
  Serial.println(" %");

  Serial.println();

  Serial.print("Free heap: ");
  Serial.print(freeHeap);
  Serial.println(" bytes");

  Serial.print("Minimum free heap: ");
  Serial.print(minimumFreeHeap);
  Serial.println(" bytes");

  Serial.print("Stack high-water mark: ");
  Serial.print(stackHighWaterMark);
  Serial.println(" words");

  Serial.println();

  Serial.println(
    "Baseline Experiment 1 complete."
  );

  Serial.println("========================================");


  // Stop task
  vTaskDelete(NULL);
}


void setup() {

  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);


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
