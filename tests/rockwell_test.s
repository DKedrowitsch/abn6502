.setcpu "6502"
.feature string_escapes

PRIMM = $ffc8
CTRL = $7000
T1CL = $6004
T1CH = $6005
IFR = $600d
ACR = $600b
TESTBYTE = $ea
TIMER_PERIOD = 7998

.segment "TEST"

start:
    sei
    cld
    lda #$ff
    sta TESTBYTE
    lda TESTBYTE
    cmp #$ff
    bne not_rockwell
    ldx #0
    lda #0

    ; $17 is RMB1 zp on a Rockwell/WDC-enhanced 65C02 and undocumented
    ; SLO zp,X on NMOS. $EA is both the test address and a harmless NOP if an
    ; unknown CMOS implementation consumes fewer operand bytes than expected.
    .byte $17, TESTBYTE, $ea, $ea

    lda TESTBYTE
    cmp #$fd
    beq rockwell

not_rockwell:
    lda #250 ; Approximately one second per phase.
    sta delay_ticks
    jsr PRIMM
    .asciiz "\nROCKWELL BIT INSTRUCTIONS NOT DETECTED.\nSLOW CTRL7 BLINK.\nRESET TO EXIT.\n"
    jmp begin_blink

rockwell:
    lda #31 ; Approximately 124 ms per phase.
    sta delay_ticks
    jsr PRIMM
    .asciiz "\nROCKWELL/WDC BIT INSTRUCTIONS DETECTED.\nFAST CTRL7 BLINK.\nRESET TO EXIT.\n"

begin_blink:
    ; Establish the VIA timebase rather than depending on inherited state.
    lda ACR
    and #$7f ; Disable PB7 Timer 1 output.
    ora #$40 ; Timer 1 continuous mode.
    sta ACR
    lda #<TIMER_PERIOD
    sta T1CL
    lda #>TIMER_PERIOD
    sta T1CH

blink:
    lda #$89 ; CTRL7 on, video enabled, VRAM at $0800.
    sta CTRL
    jsr wait_phase
    lda #$09 ; CTRL7 off with the same video configuration.
    sta CTRL
    jsr wait_phase
    jmp blink

wait_phase:
    lda T1CL ; Clear any pending Timer 1 flag.
    ldx delay_ticks
wait_tick:
    lda IFR
    and #$40
    beq wait_tick
    lda T1CL ; Clear the flag; the free-running timer has already reloaded.
    dex
    bne wait_tick
    rts

delay_ticks:
    .byte 250
