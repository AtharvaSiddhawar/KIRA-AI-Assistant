#pragma once

#include <Arduino.h>

bool kiraDiagnosticsBegin();
void kiraDiagnosticsService();

void kiraDiagnosticsPrintSummary();
void kiraDiagnosticsPrintFull();

String kiraDiagnosticsOneLine();
