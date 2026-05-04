/**
 * @file gpio_test.c
 * @brief cmd list for test GPIO function
 * 
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2025, SUNIX Co., Ltd.
 *
 * Authors      : Max Chang <max.chang@sunix.com>
 * 
 * 
**/

#include "precomp.h"

#define ESC_KEY 27
#define GPIO_CLASS_PATH "/dev/"

volatile int running = 1; 

char gpio_chip[20];
struct gpiod_chip *chip;
struct gpiod_line *line;
struct gpiod_line_event event;

struct timespec timeout;

struct termios oldt;

void set_terminal_mode() {
	struct termios newt;
    tcgetattr(STDIN_FILENO, &oldt);  
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);  
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
}

void reset_terminal_mode() {
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

void *keyboard_listener(void *arg) {
    char key;
    while (running) {
        if (read(STDIN_FILENO, &key, 1) > 0) {
            if (key == ESC_KEY) {
                printf("\nStop wating GPIO event...\n");
				running = 0;
                break;
            }
        }
        usleep(50000); 
    }
    return NULL;
}

static void _print_gpio_cmd(void)
{
	printf(">>------------------------------------<<\n");
	printf("     A : GPIO Chip Open\n");
	printf("     B : GPIO Chip Close\n");
	printf("     C : Get Chip Line List\n");
	printf("     D : Set Output Value\n");
	printf("     E : Get Input Value\n");
    printf("     F : Wait Input Event\n");
	printf("\n");
	printf("     Q : quit\n");
	printf(">>------------------------------------<<\n\n");
}

void list_gpio_chips()
 {
    struct dirent *entry;
    DIR *dir = opendir(GPIO_CLASS_PATH);

    if (!dir) {
        perror("Failed to open GPIO directory");
        return;
    }

	printf("=======================================================\r\n");
    printf("Available GPIO chips in the system:\r\n");
    printf("Name\t\tLabel\t\t\t\tLines\r\n");

    while ((entry = readdir(dir)) != NULL) {
        // Check if the entry is a gpiochip (e.g., gpiochip0, gpiochip1, etc.)
        if (strncmp(entry->d_name, "gpiochip", 8) == 0) {
            char chip_path[256];
            snprintf(chip_path, sizeof(chip_path), "%s%.250s", GPIO_CLASS_PATH, entry->d_name);

            struct gpiod_chip *chip = gpiod_chip_open(chip_path);
            if (!chip) {
                printf("%s\t\t[Failed to open]\n", entry->d_name);
                continue;
            }

            const char *label = gpiod_chip_label(chip);
            int num_lines = gpiod_chip_num_lines(chip);
            
            printf("%s\t%-30s\t%d\n", entry->d_name, label ? label : "[No Label]", num_lines);

            gpiod_chip_close(chip);
        }
    }
	printf("=======================================================\r\n");
    closedir(dir);
}

void gpio_chip_open()
{
	int n = input_gpio_device_number();
		
	sprintf(gpio_chip, "/dev/gpiochip%d", n);

	chip = gpiod_chip_open(gpio_chip);
	
	if (!chip) {
        printf("Error opening GPIO chip: %s\r\n", gpio_chip);
    }
	else
	{
		printf("Open %s Successfully\r\n", gpio_chip);
	}
}

void gpio_chip_close()
{
	if(chip)
	{
		gpiod_chip_close(chip);
		printf("%s closed\r\n",gpio_chip);
	}
}

void get_chip_lines()
{
	int num_lines = gpiod_chip_num_lines(chip);

	printf("========== %s:%d lines ==========\r\n", gpio_chip, num_lines);
	for (int i = 0; i < num_lines; i++) 
	{
		struct gpiod_line *line = gpiod_chip_get_line(chip, i);

		int direction = gpiod_line_direction(line);

		const char *dir = (direction == GPIOD_LINE_DIRECTION_INPUT) ? "Input" : (direction == GPIOD_LINE_DIRECTION_OUTPUT) ? "Output" : "Unkown";

		if (line) {			
			printf("line%d\t direction:%s\n", i, dir);
		} else {
			printf("Error to get: line%d\n", i);
		}

		gpiod_line_release(line);
	}
	printf("=============================================");
}

void gpio_chip_set_output()
{
	if(chip)
	{
		int line_num = input_line_number();
		line = gpiod_chip_get_line(chip, line_num);
		
		if(!line)
		{
			printf("Error get GPIO chip line: %d\r\n", line_num);
			gpiod_chip_close(chip);
		}

		if(gpiod_line_request_output(line, "gpio_output", 0) < 0)
		{
			printf("Error: gpiod_line_request_output\r\n");
			gpiod_chip_close(chip);
		}

		int value = input_gpio_output_level_value();

		int result = gpiod_line_set_value(line, value);

		if(result != 0)
		{
			printf("Set output value fail, line:%d, GPIO chip:%s\r\n", line_num, gpio_chip);
		}
		else
		{
			printf("Set output value successfully, line:%d, GPIO chip:%s\r\n", line_num, gpio_chip);
		}

		gpiod_line_release(line);
	}
	else
	{
		printf("Error: gpiod_chip is null\r\n");
	}
}

void gpio_chip_get_input()
{
	if(chip)
	{
		int line_num = input_line_number();
		line = gpiod_chip_get_line(chip, line_num);

		if(!line)
		{
			printf("Error get GPIO chip line: %d\r\n", line_num);
			gpiod_chip_close(chip);
		}

		if (gpiod_line_request_input(line, "gpio_input") < 0) 
		{
			printf("Error: gpiod_line_request_input\r\n");
			gpiod_chip_close(chip);
		}

		int value = gpiod_line_get_value(line);

		if(value < 0)
		{
			printf("Get input value fail, line:%d, GPIO chip:%s\r\n", line_num, gpio_chip);
		}
		else
		{
			printf("line%d input value:%d\r\n", line_num,value);
		}

		gpiod_line_release(line);
	}
	else
	{
		printf("Error: gpiod_chip is null\r\n");
	}
}

void gpio_chip_wait_input_event()
{	
	timeout.tv_sec = 0.5;
	timeout.tv_nsec = 0;

	if(chip)
	{
		int result;

		int line_num = input_line_number();
		line = gpiod_chip_get_line(chip, line_num);

		if(!line)
		{
			printf("Error get GPIO chip line: %d\r\n", line_num);
			gpiod_chip_close(chip);
		}
		
		if (gpiod_line_request_both_edges_events(line, "gpio_event") < 0)
		{
			printf("Error: gpiod_line_request_both_edge_events\r\n");
			gpiod_chip_close(chip);
		}
		
		set_terminal_mode();
		pthread_t thread;
        pthread_create(&thread, NULL, keyboard_listener, NULL);

		printf("Waiting line%d event...press 'Esc' to exit\r\n", line_num);

		running = 1;

		while (running)
		{

			result = gpiod_line_event_wait(line, &timeout);
			
			if(result > 0)
			{
				gpiod_line_event_read(line, &event);
				
				switch (event.event_type)
				{
					case GPIOD_LINE_EVENT_RISING_EDGE:
						
						printf("line%d rising edge triggered, timestamp:%" PRIdMAX ".%09" PRIdMAX "\r\n",line_num, (intmax_t)event.ts.tv_sec, (intmax_t)event.ts.tv_nsec);
						break;
					
					case GPIOD_LINE_EVENT_FALLING_EDGE:
	
						printf("line%d falling edge triggered, timestamp:%" PRIdMAX ".%09" PRIdMAX "\r\n",line_num, (intmax_t)event.ts.tv_sec, (intmax_t)event.ts.tv_nsec);
						break;
				}

			}
		}
		pthread_join(thread, NULL);
		gpiod_line_release(line);
		reset_terminal_mode();
	}
	else
	{
		printf("Error: gpiod_chip is null\r\n");
	}
}

void gpio_test(void)
{
    int cmd;

    (void)!system("clear");

	list_gpio_chips();

	_print_gpio_cmd();

    do{
		printf("\n>> CMD ?\n");
		cmd = sgetche();
		printf("\n");

		switch (cmd) {
		case 'a':
		case 'A':
			gpio_chip_open();
			break;
		case 'b':
		case 'B':
			gpio_chip_close();
			break;
		case 'c':
		case 'C':
			get_chip_lines();
			break;
		case 'd':
		case 'D':
			gpio_chip_set_output();
			break;
		case 'e':
		case 'E':
			gpio_chip_get_input();
			break;
		case 'f':
		case 'F':
			gpio_chip_wait_input_event();
			break;
		case '?':
			_print_gpio_cmd();
			break;
		}

    }while (cmd != 'q' && cmd != 'Q');
}

