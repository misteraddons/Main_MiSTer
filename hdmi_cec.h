// hdmi_cec.h - HDMI CEC interface for MiSTer
// Following ADV7513 Programming Guide for CEC implementation

#ifndef HDMI_CEC_H
#define HDMI_CEC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// CEC Initialization and Configuration
int hdmi_cec_init(void);
void hdmi_cec_deinit(void);

// CEC Message Functions
int hdmi_cec_send_message(const uint8_t* data, uint8_t length);
int hdmi_cec_receive_message(uint8_t* data, uint8_t* length);
int hdmi_cec_send_device_name(const char* name);
int hdmi_cec_send_active_source(void);
int hdmi_cec_report_physical_address(void);
int hdmi_cec_send_polling_message(void);
int hdmi_cec_request_vendor_id(void);

// CEC Status Functions
int hdmi_cec_is_connected(void);
int hdmi_cec_get_physical_address(uint16_t* address);
void hdmi_cec_monitor_status(void);
void hdmi_cec_poll_messages(void);
int hdmi_cec_check_tx_status(void);
void hdmi_cec_verify_hardware(void);

#ifdef __cplusplus
}
#endif

#endif // HDMI_CEC_H