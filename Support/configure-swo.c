void GenericsConfigureTracing(uint32_t itmChannel, uint32_t sampleInterval, uint32_t tsPrescale)
{
  /* STM32 specific configuration to enable the TRACESWO IO pin */
  RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;
  AFIO->MAPR |= (2 << 24); // Disable JTAG to release TRACESWO
  DBGMCU->CR |= DBGMCU_CR_TRACE_IOEN; // Enable IO trace pins for Async trace
  /* End of STM32 Specific instructions */

  *((volatile unsigned *)(0xE0040010)) = 31;  // Output bits at 72000000/(31+1)=2.250MHz.
  *((volatile unsigned *)(0xE00400F0)) = 2;  // Use Async mode pin protocol

  if (!itmChannel)
    {
      *((volatile unsigned *)(0xE0040304)) = 0;  // Bypass the TPIU and send output directly
    }
  else
    {
      *((volatile unsigned *)(0xE0040304)) = 0x102; // Use TPIU formatter and flush
    }

  /* Configure Trace Port Interface Unit */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // Enable access to registers

  /* Configure PC sampling and exception trace  */
  DWT->CTRL = /* CYCEVTEN */ (1<<22) |
    /* Sleep event overflow */ (1<<19)  |
    /* Enable Exception Trace */ (1 << 16) |
    /* PC Sample event */  (1<<12) |
    /* Sync Packet Interval */ ((3&0x03) << 10) | /* 0 = Off, 1 = 2^23, 2 = Every 2^25, 3 = 2^27 */
    /* CYCTap position */ (1 << 9) |  /* 0 = x32, 1=x512 */
    /* Postscaler for PC sampling */ ((sampleInterval&0x0f) << 1) | /* Divider = value + 1 */
    /* CYCCnt Enable */ (1 << 0);

  /* Configure instrumentation trace macroblock */
  ITM->LAR = ETM_LAR_KEY;
  ITM->TCR = /* DWT Stimulus */ (1<<3)|
    /* ITM Enable */   (1<<0)|
    /* SYNC Enable */  (1<<2)|
    /* TS Enable */    (1<<1)|
    /* TC Prescale */  ((tsPrescale&0x03)<<8) |
    ((itmChannel&0x7f)<<16);  /* Set trace bus ID and enable ITM */
  ITM->TER = 0xFFFFFFFF; // Enable all stimulus ports
  
  /* Configure embedded trace macroblock */
  ETM->LAR = ETM_LAR_KEY;
  ETM_SetupMode();
  ETM->CR = ETM_CR_ETMEN // Enable ETM output port
    | ETM_CR_STALL_PROCESSOR // Stall processor when fifo is full
    | ETM_CR_BRANCH_OUTPUT; // Report all branches
  ETM->TRACEIDR = 2; // Trace bus ID for TPIU
  ETM->TECR1 = ETM_TECR1_EXCLUDE; // Trace always enabled
  ETM->FFRR = ETM_FFRR_EXCLUDE; // Stalling always enabled
  ETM->FFLR = 24; // Stall when less than N bytes free in FIFO (range 1..24)
  // Larger values mean less latency in trace, but more stalls.
}
