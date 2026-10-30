.setcpu "65C02"
.feature string_escapes

PRIMM = $ffc8
CTRL = $7000
T1CL = $6004
IFR = $600d

.segment "TEST"

start:
    cli
    jsr PRIMM
    .asciiz "\nWIRELESS UPLOAD TEST PASSED!\nCTRL7 NOW BLINKS ONCE PER SECOND.\nRESET TO EXIT.\n"

    sei ; Stop the ROM heartbeat so this program controls CTRL7.

blink:
    lda #$89 ; CTRL7 on, with video enabled and VRAM at $0800.
    sta CTRL
    jsr wait_one_second
    lda #$09 ; CTRL7 off, preserving the same video configuration.
    sta CTRL
    jsr wait_one_second
    bra blink

wait_one_second:
    lda T1CL ; Clear any pending Timer 1 interrupt flag.
    ldx #250 ; 250 periods at approximately 4 ms each.
wait_tick:
    lda IFR
    and #$40
    beq wait_tick
    lda T1CL ; Clear the Timer 1 flag and begin the next period.
    dex
    bne wait_tick
    rts
