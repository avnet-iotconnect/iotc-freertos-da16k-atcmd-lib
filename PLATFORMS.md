# Platform-specific Configuration Options

This document will be continuously expanded as support for additional platforms is added.

---

## Renesas CK-RA6M5v2 / EK-RA6M4

The configuration must define one of the following:
* `DA16K_CONFIG_EK_RA6M4` (for the EK-RA6M4 target)
* `DA16K_CONFIG_CK_RA6M5` (for the CK-RA6M5v2 target)

The configuration MUST also define the following value:
* `DA16K_CONFIG_RENESAS_SCI_UART_CHANNEL` (The UART channel number used for the AT protocol communication. Note that this must be set up in the board/project stack correctly, else the project will fail to build.)

---