#ifndef _J1939_PROG_SXXG_H_
#define _J1939_PROG_SXXG_H_

#define J1939_SA                            0           /*!< J1939 source address*/
#define J1939_DA                            1           /*!< J1939 source address*/

#define INPUT_PARAMTER_DISABLE				0			/*!< disable user input */
#define INPUT_PARAMTER_ENABLE				1			/*!< enable user input */

#define RANDOM_DATA_DISABLE					0			/*!< disable random data */
#define RANDOM_DATA_ENABLE					1			/*!< enable random data */

#define J1939_DATA_LEN                      8           /*!< J1939 data length*/
#define BUFF_LEN							64			/*!< Buffer length */
#define MAXIMUM_IDFILTER_AMOUNT				32          /*!< The maximum limit of id filter */

// global using
/*!< J1939 device - Socket descriptor */
extern int sd;
/*!< J1939 device - name */
extern char can_net_name[BUFF_LEN];

extern struct sockaddr_can gcan_addr;

/*!< J1939 device - recv thread create flag */
extern int revthd_create_flag;
/*!< J1939 device - recv thread handle */
extern pthread_t revthd_hd;

#endif