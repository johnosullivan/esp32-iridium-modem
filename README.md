# ESP32-SX/CX Iridium Modem

## Overview

The purpose of this library is to provide a drop-in solution for interactions on the Iridium Satellite network using AT commands. The library uses the ESP32 UART bus with different queues/pthread to mange messages in an asynchronous or synchronous way.

## Dependencies 

- <a href="https://github.com/espressif/esp-idf/blob/master/tools/idf.py">ESP IDF</a>
- <a href="https://www.freertos.org">FreeRTOS</a>

## Documentation

Construct an iridium satellite communication struct with callbacks.

```c
iridium_t *satcom = iridium_default_configuration();
satcom->callback = &cb_satcom;
satcom->message_callback = &cb_message;
```

Configure UART bus ports.

```c
/* UART Port Configuration */
satcom->uart_number = UART_NUM_1;
satcom->uart_txn_number = GPIO_NUM_17;
satcom->uart_rxd_number = GPIO_NUM_18;
satcom->uart_rts_number = UART_PIN_NO_CHANGE;
satcom->uart_cts_number = UART_PIN_NO_CHANGE;
```

Setup satellite modem.

```c
iridium_config(satcom) // return SAT_OK or SAT_ERROR
```

Once a `SAT_OK` status is received from the satellite configuration, the following methods are available.

---

Enabled or disable the ring notification on the modem.
```c
/**
 * @param satcom the iridium_t struct pointer.
 * @param enabled the ring notification.
 * @return a iridium_result_t with metadata.
 */
iridium_result_t iridium_config_ring(iridium_t *satcom, bool enabled);
```

---
Transmit a message to the iridium network.
```c
/**
 * @param satcom the iridium_t struct pointer.
 * @param message to be sent.
 * @return a iridium_result_t with metadata.
 */
iridium_result_t iridium_tx_message(iridium_t *satcom, char *message);
```

---
Send AT command with data.
```c
/**
 * @param satcom the iridium_t struct pointer.
 * @param command the iridium modem AT command.
 * @param rdata the raw data. 
 * @param wait_response wait for a responce from the modem.
 * @param wait_interval the amount of time in ms for wait interval check.
 * @return a iridium_result_t with metadata.
 */
iridium_result_t iridium_send(iridium_t* satcom, iridium_command_t command, char *rdata, bool wait_response, int wait_interval);
```

## Example

```c
#include "iridium.h"

static const char *TAG_CORE = "iridium_example";

/* Callbacks */
void cb_satcom(iridium_t* satcom, iridium_command_t command, iridium_status_t status) { 
    if (status == SAT_OK) {
        switch (command) {
            case AT_CSQ:
                ESP_LOGI(TAG_CORE, "Signal Strength [0-5]: %d", satcom->signal_strength);
                break;
            case AT_CGMM:
                ESP_LOGI(TAG_CORE, "Model Identification: %s", satcom->model_identification);
                break;
            case AT_CGMI:
                ESP_LOGI(TAG_CORE, "Manufacturer Identification: %s", satcom->manufacturer_identification);
                break;
            default:
                break;
        }
    }
}

void cb_message(iridium_t* satcom, char* data) { 
    ESP_LOGI(TAG_CORE, "MESSAGE[INCOMING] %s", data);
}

/* Configuration Iridium SatCom */
iridium_t *satcom = iridium_default_configuration();
satcom->callback = &cb_satcom;
satcom->message_callback = &cb_message;
/* UART Port Configuration */
satcom->uart_number = UART_NUM_1;
satcom->uart_txn_number = GPIO_NUM_17;
satcom->uart_rxd_number = GPIO_NUM_18;
satcom->uart_rts_number = UART_PIN_NO_CHANGE;
satcom->uart_cts_number = UART_PIN_NO_CHANGE;
    
/* Initialized */
if (iridium_config(satcom) == SAT_OK) {
    ESP_LOGI(TAG_CORE, "Iridium Modem [Initialized]");
}

/* Allow Ring Triggers */
iridium_result_t ring = iridium_config_ring(satcom, true);
if (ring.status == SAT_OK) {
    ESP_LOGI(TAG_CORE, "Iridium Modem [Ring Enabled]");
}
```

# Contributing

When contributing to this repository, please first discuss the change you wish to make via issue,
email, or any other method with the owners of this repository before making a change. 

Please note we have a code of conduct, please follow it in all your interactions with the project.

## Pull Request Process

1. Ensure any install or build dependencies are removed before the end of the layer when doing a 
   build.
2. Update the README.md with details of changes to the interface, this includes new environment 
   variables, exposed ports, useful file locations and container parameters.
3. You may merge the Pull Request in once you have the sign-off of two other developers, or if you 
   do not have permission to do that, you may request the second reviewer to merge it for you.

## Code of Conduct

### Our Pledge

In the interest of fostering an open and welcoming environment, we as
contributors and maintainers pledge to making participation in our project and
our community a harassment-free experience for everyone, regardless of age, body
size, disability, ethnicity, gender identity and expression, level of experience,
nationality, personal appearance, race, religion, or sexual identity and
orientation.

### Our Standards

Examples of behavior that contributes to creating a positive environment
include:

* Using welcoming and inclusive language
* Being respectful of differing viewpoints and experiences
* Gracefully accepting constructive criticism
* Focusing on what is best for the community
* Showing empathy towards other community members

Examples of unacceptable behavior by participants include:

* The use of sexualized language or imagery and unwelcome sexual attention or
advances
* Trolling, insulting/derogatory comments, and personal or political attacks
* Public or private harassment
* Publishing others' private information, such as a physical or electronic
  address, without explicit permission
* Other conduct which could reasonably be considered inappropriate in a
  professional setting

### Our Responsibilities

Project maintainers are responsible for clarifying the standards of acceptable
behavior and are expected to take appropriate and fair corrective action in
response to any instances of unacceptable behavior.

Project maintainers have the right and responsibility to remove, edit, or
reject comments, commits, code, wiki edits, issues, and other contributions
that are not aligned to this Code of Conduct, or to ban temporarily or
permanently any contributor for other behaviors that they deem inappropriate,
threatening, offensive, or harmful.

### Scope

This Code of Conduct applies both within project spaces and in public spaces
when an individual is representing the project or its community. Examples of
representing a project or community include using an official project e-mail
address, posting via an official social media account, or acting as an appointed
representative at an online or offline event. Representation of a project may be
further defined and clarified by project maintainers.

### Enforcement

Instances of abusive, harassing, or otherwise unacceptable behavior may be
reported by contacting the project team at [INSERT EMAIL ADDRESS]. All
complaints will be reviewed and investigated and will result in a response that
is deemed necessary and appropriate to the circumstances. The project team is
obligated to maintain confidentiality with regard to the reporter of an incident.
Further details of specific enforcement policies may be posted separately.

Project maintainers who do not follow or enforce the Code of Conduct in good
faith may face temporary or permanent repercussions as determined by other
members of the project's leadership.

### Attribution

This Code of Conduct is adapted from the [Contributor Covenant][homepage], version 1.4,
available at [http://contributor-covenant.org/version/1/4][version]

[homepage]: http://contributor-covenant.org
[version]: http://contributor-covenant.org/version/1/4/
