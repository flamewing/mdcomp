# Full Cartesian matrices within the declared tested unroll range 0..3.
foreach(unroll RANGE 0 3)
    if(NOT unroll EQUAL 3)
        decoder_image(comper-u${unroll} comper m68000 Comper.asm CompDec false false
            CONFIGURATION "unroll=${unroll}" DEFINES _Comp_LoopUnroll=${unroll})
        decoder_image(kosplus-u${unroll} kosplus m68000 KosinskiPlus.asm KosPlusDec false false
            CONFIGURATION "unroll=${unroll}" DEFINES _KosPlus_LoopUnroll=${unroll})
    endif()
    foreach(lut RANGE 0 1)
        if(NOT unroll EQUAL 3 OR NOT lut EQUAL 1)
            decoder_image(rocket-u${unroll}-lut${lut} rocket m68000 Rocket.asm RocketDec false false
                CONFIGURATION "unroll=${unroll},lut=${lut}"
                DEFINES _Rocket_LoopUnroll=${unroll} _Rocket_UseLUT=${lut})
        endif()
        foreach(extreme RANGE 0 1)
            if(NOT unroll EQUAL 3 OR NOT lut EQUAL 1 OR NOT extreme EQUAL 1)
                decoder_image(kosinski-u${unroll}-lut${lut}-x${extreme} kosinski m68000 Kosinski.asm KosDec false false
                    CONFIGURATION "unroll=${unroll},lut=${lut},extreme=${extreme}"
                    DEFINES _Kos_LoopUnroll=${unroll} _Kos_UseLUT=${lut} _Kos_ExtremeUnrolling=${extreme})
            endif()
        endforeach()
    endforeach()
endforeach()
foreach(lut RANGE 0 1)
    foreach(aligned RANGE 0 1)
        if(NOT lut EQUAL 1 OR NOT aligned EQUAL 1)
            math(EXPR scratch "2 * (1 - ${aligned})")
            decoder_image(saxman-lut${lut}-aligned${aligned} saxman m68000 Saxman.asm SaxDec false false
                CONFIGURATION "lut=${lut},aligned=${aligned},source=even" STACK_SCRATCH ${scratch}
                DEFINES _Sax_UseLUT=${lut} _Sax_AlignedSource=${aligned})
        endif()
    endforeach()
    decoder_image(saxman-lut${lut}-odd saxman m68000 Saxman.asm SaxDec false false
        PAYLOAD_OFFSET 1 CONFIGURATION "lut=${lut},aligned=0,source=odd" STACK_SCRATCH 2
        DEFINES _Sax_UseLUT=${lut} _Sax_AlignedSource=0)
endforeach()
decoder_image(comperx-jump comperx m68000 ComperX.asm ComperXDec false false
    JUMP_RETURN CONFIGURATION "return=a5" DEFINES comperx_jump_to_reg_on_end=1)
