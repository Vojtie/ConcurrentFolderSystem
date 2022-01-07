#pragma once

/* wypisuje informacje o błędnym zakończeniu funkcji systemowej
i kończy działanie */
extern void syserr(const char* fmt, ...);

/* wypisuje informacje o błędzie i kończy działanie */
extern void fatal(const char* fmt, ...);
