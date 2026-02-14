#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class GfxRenderer;
class MappedInputManager;

namespace DeveloperTests {
void runDisplayResponseTest(GfxRenderer& renderer, MappedInputManager& mappedInput, SemaphoreHandle_t renderingMutex);
}  // namespace DeveloperTests
