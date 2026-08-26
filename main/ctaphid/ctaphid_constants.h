#ifndef CTAPHID_CONSTANTS_H
#define CTAPHID_CONSTANTS_H

/* CTAPHID Constants */
#define CTAPHID_BROADCAST_CID 0xFFFFFFFF

/* CTAPHID Packet Types */
#define CTAPHID_TYPE_INIT 0x80
#define CTAPHID_TYPE_CONT 0x00

/* CTAPHID Commands (Bit 7 is set) */
#define CTAPHID_CMD_PING      0x81
#define CTAPHID_CMD_MSG       0x83 /* U2F message */
#define CTAPHID_CMD_LOCK      0x84
#define CTAPHID_CMD_INIT      0x86
#define CTAPHID_CMD_WINK      0x88
#define CTAPHID_CMD_CBOR      0x90 /* CTAP2 CBOR command */
#define CTAPHID_CMD_CANCEL    0x91
#define CTAPHID_CMD_KEEPALIVE 0xBB
#define CTAPHID_CMD_ERROR     0xBF

/* CTAPHID Error Codes (Payload for CMD_ERROR) */
#define CTAPHID_ERR_INVALID_CMD     0x01 /* The command in the request is invalid */
#define CTAPHID_ERR_INVALID_PAR     0x02 /* The parameter(s) in the request is invalid */
#define CTAPHID_ERR_INVALID_LEN     0x03 /* The length field (BCNT) is invalid for the request */
#define CTAPHID_ERR_INVALID_SEQ     0x04 /* The sequence does not match expected value */
#define CTAPHID_ERR_MSG_TIMEOUT     0x05 /* The message has timed out */
#define CTAPHID_ERR_CHANNEL_BUSY    0x06 /* The device is busy for the requesting channel */
#define CTAPHID_ERR_LOCK_REQUIRED   0x0A /* Command requires channel lock */
#define CTAPHID_ERR_INVALID_CHANNEL 0x0B /* CID is not valid */
#define CTAPHID_ERR_OTHER           0x7F /* Unspecified error */

/* Protocol Configuration Limits */
#define CTAPHID_INIT_PAYLOAD_SIZE 57
#define CTAPHID_CONT_PAYLOAD_SIZE 59
#define CTAPHID_MAX_MSG_SIZE      7609

#endif /* CTAPHID_CONSTANTS_H */