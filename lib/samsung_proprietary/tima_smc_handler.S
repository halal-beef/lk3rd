.section .tima_smc, "a"
.global tima_smc_handler
tima_smc_handler:
    .align 4
    .incbin "../../../../lib/samsung_proprietary/tima_smc_handler_blob.bin"

.globl tima_smc_handler_end
spi_csmc_hdr_end:
