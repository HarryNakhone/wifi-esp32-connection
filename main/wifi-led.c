#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wpa2.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include <sys/socket.h>
#include <stdbool.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>


#define WIFI_SUCCESS 1 << 0
#define WIFI_FAILURE 1 << 1
#define TCP_SUCCESS 1 << 0
#define TCP_FAILURE 1 << 1024
#define MAX_FAILURES 10

#define WIFI_USERNAME "YO USERNAME"
#define WIFI_PASSWORD "YO PASSWORD"

#define BUFFER_SIZE 1024

static EventGroupHandle_t wifi_event_group;

static int s_retry_num = 0;

static const char * TAG = "WIFI";



static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void* event_data){

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START){
		ESP_LOGI(TAG, "Connecting to AP...");
		esp_wifi_connect();
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED){
		if (s_retry_num < MAX_FAILURES){
			ESP_LOGI(TAG, "Reconnecting to AP...");
			esp_wifi_connect();
			s_retry_num++;
		}else {
			xEventGroupSetBits(wifi_event_group, WIFI_FAILURE);
		}
	}
}

static void ip_event_handler(void * arg, esp_event_base_t event_base, int32_t event_id, void * event_data){
	if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP){
		ip_event_got_ip_t * event = (ip_event_got_ip_t*) event_data;
		ESP_LOGI(TAG, "STA IP: " IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;
		xEventGroupSetBits(wifi_event_group, WIFI_SUCCESS);
	}
}



esp_err_t connect_wifi(){
	int status = WIFI_FAILURE;

	ESP_ERROR_CHECK(esp_netif_init());

	ESP_ERROR_CHECK(esp_event_loop_create_default());

	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	wifi_event_group = xEventGroupCreate();

	esp_event_handler_instance_t wifi_handler_e_inst;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
															&wifi_event_handler, NULL, &wifi_handler_e_inst));

	esp_event_handler_instance_t got_ip_e_inst;
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
															 &ip_event_handler, NULL, &got_ip_e_inst));


	wifi_config_t wifi_config = {
		.sta = {
			.ssid = "YO WIFI NAME OR SSID", 
			

		},
	};

	ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_enable());
	
	ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_set_identity((uint8_t *) WIFI_USERNAME, strlen(WIFI_USERNAME)));

	ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_set_username((uint8_t *) WIFI_USERNAME, strlen(WIFI_USERNAME) ));

	ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_set_password((uint8_t *) WIFI_PASSWORD, strlen(WIFI_PASSWORD) ));

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));



	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "STA initializaton complete");

	EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
			WIFI_SUCCESS | WIFI_FAILURE,
			pdFALSE,
			pdFALSE,
			portMAX_DELAY);

	if (bits & WIFI_SUCCESS){
		ESP_LOGI(TAG, "Connect to AP");
		status = WIFI_SUCCESS;
	} else if (bits & WIFI_FAILURE){
		ESP_LOGI(TAG, "Failed to connect to AP");
		status = WIFI_FAILURE;
	} else{
		ESP_LOGI(TAG, "UNEXPECTED EVENT");
		status = WIFI_FAILURE;
	}

	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, got_ip_e_inst));
	ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler_e_inst));
	vEventGroupDelete(wifi_event_group);

	return status;


}

esp_err_t connect_tcp_server(void){
	int socketFD = socket(AF_INET, SOCK_STREAM, 0);

	char readBuffer[BUFFER_SIZE] = {0};


	struct sockaddr_in serverAddr = {0};

	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = inet_addr("172.20.43.190");
	serverAddr.sin_port = htons(5823);

	
	if (socketFD < 0){

        printf("failed to create socket");
		ESP_LOGE(TAG, "Failed to create a socket..?");
		return EXIT_FAILURE;

	}

	if(connect(socketFD, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) != 0){

        printf("Fail to connect tcp fail, %s \n", inet_ntoa(serverAddr.sin_addr));

		ESP_LOGE(TAG, "Failed to connect to %s!", inet_ntoa(serverAddr.sin_addr.s_addr));
		close(socketFD);
		return EXIT_FAILURE;
	}

	ESP_LOGI(TAG, "Connect to TCP server.");
    printf("Connect to TCP server \n");


    while (1){

	memset(readBuffer, 0, BUFFER_SIZE);
	
	int bytesRead = read(socketFD, readBuffer, sizeof(readBuffer) - 1);
	
	if (bytesRead <= 0){
		ESP_LOGE(TAG, "Server disconnected or error occured\n");
		break;
	}

	readBuffer[bytesRead] = '\0';

	ESP_LOGI(TAG, "Server: %s\n", readBuffer);

	char * substr;

	if((substr = strstr(readBuffer, "ON")) != NULL){
				
		ESP_LOGI(TAG, "Received Command : %s\n", substr);
	}

	substr = NULL;
    }

    ESP_LOGI(TAG, "Connect closed");
    close(socketFD);

	return TCP_SUCCESS;
}


void app_main(void)
{
	esp_err_t status = WIFI_FAILURE;

	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND){
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);


	status = connect_wifi();
	if (WIFI_SUCCESS != status){
		ESP_LOGI(TAG, "Failed to associate to AP, dying ... ");
		return;

	}

	status = connect_tcp_server();
	if (TCP_SUCCESS != status){
		ESP_LOGI(TAG, "Failed to connect to remote server, dying...");
		return;
	}
}
