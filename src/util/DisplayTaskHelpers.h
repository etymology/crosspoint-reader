// DisplayTaskHelpers.h
// Shared display-task lifecycle helpers used by all reader activities.
// Each activity runs a FreeRTOS task that polls an updateRequired flag,
// renders under mutex protection, and cleans up on exit.
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace DisplayTaskHelpers {

/// Standard display loop: polls updateRequired, renders under mutex.
template <typename RenderFn>
[[noreturn]] void displayLoop(bool& updateRequired, SemaphoreHandle_t mutex, RenderFn render) {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(mutex, portMAX_DELAY);
      render();
      xSemaphoreGive(mutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/// Display loop with a post-render callback (e.g. LongPressHandler::onRenderComplete).
template <typename RenderFn, typename PostRenderFn>
[[noreturn]] void displayLoop(bool& updateRequired, SemaphoreHandle_t mutex, RenderFn render,
                              PostRenderFn postRender) {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(mutex, portMAX_DELAY);
      render();
      xSemaphoreGive(mutex);
      postRender();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/// Display loop with post-render callback and a guard predicate.
/// When canRender returns false, updateRequired is preserved and rendering is skipped.
/// Used for activities whose sub-activity temporarily owns the screen.
template <typename RenderFn, typename PostRenderFn, typename CanRenderFn>
[[noreturn]] void displayLoop(bool& updateRequired, SemaphoreHandle_t mutex, RenderFn render,
                              PostRenderFn postRender, CanRenderFn canRender) {
  while (true) {
    if (updateRequired && canRender()) {
      updateRequired = false;
      xSemaphoreTake(mutex, portMAX_DELAY);
      render();
      xSemaphoreGive(mutex);
      postRender();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

/// Safely stop the display task and destroy the rendering mutex.
inline void stopTask(SemaphoreHandle_t& mutex, TaskHandle_t& taskHandle) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  if (taskHandle) {
    vTaskDelete(taskHandle);
    taskHandle = nullptr;
  }
  vSemaphoreDelete(mutex);
  mutex = nullptr;
}

}  // namespace DisplayTaskHelpers
