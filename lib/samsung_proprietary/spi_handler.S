.section .spi_smc, "a"
.global spi_handler
spi_handler:
    .align 4
    .incbin "../../../../lib/samsung_proprietary/spi_handler_blob.bin"

.globl spi_handler_end
spi_handler_end:
