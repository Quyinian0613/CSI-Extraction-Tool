/* WiFi AP / UDP server who responds with packets containing CSI preambles

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"
#include "protocol_examples_common.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "structures.h"
#include "math.h"

#define EXAMPLE_ESP_WIFI_SSID      "example_ssid"		// AP SSID name
#define EXAMPLE_ESP_WIFI_PASS      ""	// AP password
#define PORT 3333										// or any other unused port, but same as in udp_client
#define EXAMPLE_MAX_STA_CONN       20
#define LEN_MAC_ADDR 20


static const char *TAG = "wifi softAP";
static uint32_t serial_num = 0; //serial number of csi_recv
static uint32_t serial_num_pre = 0; //serial number of csi_recv
static int rssi_pre = 0;
static bool can_print = 1;

wifi_csi_info_t pre_received;


int8_t csi_buf_pre[384];




static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED)
	{
		ESP_LOGI(TAG, "Someone wants to connect!");
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d", MAC2STR(event->mac), event->aid);
	}
	else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
	{
		ESP_LOGI(TAG, "Someone disconnected...");
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" leave, AID=%d", MAC2STR(event->mac), event->aid);
    }
}

void print_csi(wifi_csi_info_t received, int diff)
{
    while(can_print == 0){}
    can_print = 0;
    printf("diff = %d\n", diff);
    for(int i = 0; i < diff; i++) {
        printf("serial_num:,%d,%d,%d,%d,%d,", serial_num_pre + i, received.len, rssi_pre, received.rx_ctrl.noise_floor, received.rx_ctrl.rx_state);
        //printf("serial_num:,%d,0,0,0,0,", serial_num_pre + i);
        for(int i = 0; i < 128; i+=2)
        {
            	int imag = csi_buf_pre[i];
            	int real = csi_buf_pre[i+1];
           	printf("%d,%d,", real, imag);
            	csi_buf_pre[i] = *(received.buf + i);
            	csi_buf_pre[i+1] = *(received.buf + i + 1);
		rssi_pre = received.rx_ctrl.rssi;

        }
        printf("\n\n");
    }
    serial_num_pre = serial_num;
    can_print = 1;

}

void receive_csi_cb(void *ctx, wifi_csi_info_t *data)
{
	wifi_csi_info_t received = data[0];

	char senddMacChr[LEN_MAC_ADDR] = {0}; // Sender
	sprintf(senddMacChr, "%02X:%02X:%02X:%02X:%02X:%02X", received.mac[0], received.mac[1], received.mac[2], received.mac[3], received.mac[4], received.mac[5]);
	if(strcmp(senddMacChr, "24:01:01:01:01:01") == 0)
    {
        int diff = serial_num + (~serial_num_pre + 1);
        printf("diff =%d\n", diff);
        if (diff == 0) {

            for(int i = 0; i < 384; i+=2)
            {
                csi_buf_pre[i] = *(received.buf + i);
                csi_buf_pre[i+1] = *(received.buf + i + 1);
            }

          printf("Update csi_buf_pre\n");
        } else if (diff >= 1) {
            print_csi(received, diff);
        } else {
            print_csi(received, 1);
        }
	}
	else
	{
        //printf("receive_csi_cb:received and dropped from %s\n", senddMacChr);
	}
}


/*
 * Receives all frames, would they contain CSI preambles or not.
 * It gets the content of the frame, not the preamble.
 */
void promi_cb(void *buff, wifi_promiscuous_pkt_type_t type)
{
	const wifi_promiscuous_pkt_t *ppkt = (wifi_promiscuous_pkt_t *)buff;
	const wifi_ieee80211_packet_t *ipkt = (wifi_ieee80211_packet_t *)ppkt->payload;
	const wifi_ieee80211_mac_hdr_t *hdr = &ipkt->hdr;
	uint8_t* my_ptr=ipkt;

	char senddMacChr[LEN_MAC_ADDR] = {0}; // Sender
	char recvdMacChr[LEN_MAC_ADDR] = {0}; // Receiver
	sprintf(recvdMacChr, "%02X:%02X:%02X:%02X:%02X:%02X", hdr->addr1[0], hdr->addr1[1], hdr->addr1[2], hdr->addr1[3], hdr->addr1[4], hdr->addr1[5]);
	sprintf(senddMacChr, "%02X:%02X:%02X:%02X:%02X:%02X", hdr->addr2[0], hdr->addr2[1], hdr->addr2[2], hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);

	// This filters for packets between your two boards
	// Choose MACs accordingly, or simply ignore
	if(strcmp(senddMacChr, "24:01:01:01:01:01") == 0 && strcmp(recvdMacChr, "24:00:00:00:00:00") == 0)
	{
		while (can_print == 0){}
		can_print = 0;
		printf("promi_cb====> Sender: %s, Receiver: %s \n", senddMacChr, recvdMacChr);
		can_print = 1;
	} else {
	    //printf("promi_cb:received and dropped from %s %s\n", senddMacChr, recvdMacChr);
	}
}


/*
 * Setting up a UDP server
 */
static void udp_server_task(void)
{
    uint32_t rx_num;
    char addr_str[128];
    int addr_family;
    int ip_protocol;

    while (1)
	{
		#ifdef CONFIG_EXAMPLE_IPV4
			struct sockaddr_in dest_addr;
			dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
			dest_addr.sin_family = AF_INET;
			dest_addr.sin_port = htons(PORT);
			addr_family = AF_INET;
			ip_protocol = IPPROTO_IP;
			inet_ntoa_r(dest_addr.sin_addr, addr_str, sizeof(addr_str) - 1);
		#else // IPV6
			struct sockaddr_in6 dest_addr;
			bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
			dest_addr.sin6_family = AF_INET6;
			dest_addr.sin6_port = htons(PORT);
			addr_family = AF_INET6;
			ip_protocol = IPPROTO_IPV6;
			inet6_ntoa_r(dest_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
		#endif

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0)
		{
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

        int err = bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err < 0)
		{
            ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        }
        ESP_LOGI(TAG, "Socket bound, port %d", PORT);

	    ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(&receive_csi_cb, NULL));    
        while (1)
		{
            ESP_LOGI(TAG, "Waiting for data");
            struct sockaddr_in6 source_addr;
            socklen_t socklen = sizeof(source_addr);
            int len = recvfrom(sock, &rx_num, sizeof(uint32_t), 0, (struct sockaddr *)&source_addr, &socklen);

            if (len < 0)
			{
                ESP_LOGE(TAG, "recvfrom failed: errno %d", errno);
                break;
            }      
            serial_num = rx_num;
            printf("udp_server_task=====> recvfrom succ;\n");
        }

        if (sock != -1)
		{
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
}
/*
 * Setting up a WiFi AP
 */
void wifi_init_softap(void)
{
	ESP_LOGI(TAG, "Entering wifi_init_softap");
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	cfg.csi_enable = 1;
	ESP_LOGI(TAG, "Starting esp_wifi_init");
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    wifi_config_t wifi_config =
	{
        .ap =
		{
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_OPEN
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0)
	{
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

	ESP_LOGI(TAG, "Starting esp_wifi_set_mode");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_AP, WIFI_PROTOCOL_11B|WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N));

    // Set a fixed MAC, Tx AMPDU has to be disabled
    uint8_t mac[6]={0x24, 0x00, 0x00, 0x00, 0x00, 0x00};
    ESP_ERROR_CHECK(esp_wifi_set_mac(ESP_IF_WIFI_AP, mac));
    
    //choose rate
    //esp_wifi_config_espnow_rate(WIFI_IF_AP, WIFI_PHY_RATE_MCS0_LGI);
    
 	ESP_LOGI(TAG, "Starting esp_wifi_start");
    ESP_ERROR_CHECK(esp_wifi_start());
    
    //set wifi chanel
    uint8_t primary = 13;
    ESP_ERROR_CHECK(esp_wifi_set_channel(primary, WIFI_SECOND_CHAN_NONE));

    //uint8_t primary_test;
    //wifi_second_chan_t second_test;
    //esp_wifi_get_channel(&primary_test, &second_test);
    //printf("serial_num primary_test:%d \n", primary_test);

    //Close wifi power saving mode
    esp_wifi_set_ps(WIFI_PS_NONE);
    
    // Choose rate
    /*esp_err_t esp_wifi_internal_set_fix_rate(wifi_interface_t ifx, bool en, wifi_phy_rate_t rate);
    ESP_ERROR_CHECK(esp_wifi_internal_set_fix_rate(ESP_IF_WIFI_AP, 1, WIFI_PHY_RATE_11M_S));*/
    
    //////////////////////////////
    
    wifi_promiscuous_filter_t filer_promi;
	wifi_promiscuous_filter_t filer_promi_ctrl;

	uint32_t filter_promi_field = (0xFFFFFFF6);
	uint32_t filter_promi_ctrl_field = (0x20000000);
	uint32_t filter_event=WIFI_EVENT_MASK_ALL;

	filer_promi.filter_mask = filter_promi_field;
	filer_promi_ctrl.filter_mask = filter_promi_ctrl_field;

	esp_wifi_set_promiscuous_filter(&filer_promi);
	esp_wifi_set_event_mask(filter_event);
	esp_wifi_set_promiscuous_ctrl_filter(&filer_promi_ctrl);

	ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
	esp_wifi_set_promiscuous_rx_cb(promi_cb);
//csi_cb

	ESP_ERROR_CHECK(esp_wifi_set_csi(1));

	// Set CSI configuration to whatever suits you best
	wifi_csi_config_t configuration_csi;
	configuration_csi.lltf_en = 1;
	configuration_csi.htltf_en = 1;
	configuration_csi.stbc_htltf2_en = 1;
	configuration_csi.ltf_merge_en = 1;
	configuration_csi.channel_filter_en = 0;
	configuration_csi.manu_scale = 0;
	//configuration_csi.shift = 0;

	ESP_ERROR_CHECK(esp_wifi_set_csi_config(&configuration_csi));
	
	//ESP_ERROR_CHECK(esp_wifi_set_csi_rx_cb(&receive_csi_cb, NULL));

    ///////////////////////////////

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s", EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);

	uint8_t mac_STA[6];
	esp_wifi_get_mac(ESP_IF_WIFI_STA, mac_STA);
	char mac_STA_str[LEN_MAC_ADDR] = {0};
	sprintf(mac_STA_str, "%02X:%02X:%02X:%02X:%02X:%02X", mac_STA[0], mac_STA[1], mac_STA[2], mac_STA[3], mac_STA[4], mac_STA[5]);
	ESP_LOGI(TAG, "MAC STA:%s", mac_STA_str);

	uint8_t mac_AP[6];
	ESP_ERROR_CHECK(esp_wifi_get_mac(ESP_IF_WIFI_AP, mac_AP));
	char mac_AP_str[LEN_MAC_ADDR] = {0};
	sprintf(mac_AP_str, "%02X:%02X:%02X:%02X:%02X:%02X", mac_AP[0], mac_AP[1], mac_AP[2], mac_AP[3], mac_AP[4], mac_AP[5]);
	ESP_LOGI(TAG, "MAC AP:%s", mac_AP_str);
        
    udp_server_task();
}


void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
    wifi_init_softap();
}
