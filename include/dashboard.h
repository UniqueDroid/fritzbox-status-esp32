#pragma once

// Dashboard lifecycle hooks.

// Initialize LVGL, register display driver, create UI and start API polling task.
void initDashboard();

// Process portal/web events, input handling and periodic UI refresh.
void loopDashboard();
