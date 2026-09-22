#include "stdlib.h"
#include "stdio.h"
#include "string.h"
#include "window.h"
#include "memory.h"
#include "graphics.h"
#include "gui/gui.h"
#include "gui/element.h"
#include "gui/image_element.h"
#include "gui/plane.h"
#include "gui/button.h"
#include "gui/textfield.h"
#include "image.h"
#include "font.h"

enum
{
    CALCULATOR_PARSING_LEFT_NUMBER = 0,
    CALCULATOR_PARSING_RIGHT_NUMBER = 1,
};

#define RESULT_TEXTFIELD 1000
#define BUTTON_DIVIDE 1001
#define BUTTON_MULTIPLY 1002
#define BUTTON_ADD 1003
#define BUTTON_SUBTRACT 1004
#define BUTTON_EQUALS 1005
#define BUTTON_DELETE 1006

#define BUTTON_7 7
#define BUTTON_8 8
#define BUTTON_9 9
#define BUTTON_4 4
#define BUTTON_5 5
#define BUTTON_6 6
#define BUTTON_1 1
#define BUTTON_2 2
#define BUTTON_3 3
#define BUTTON_0 0

typedef int BUTTON_ID;

// Loop through this array to create the number buttons
BUTTON_ID button_ids[] = {BUTTON_7, BUTTON_8, BUTTON_9, BUTTON_4, BUTTON_5, BUTTON_6, BUTTON_1, BUTTON_2, BUTTON_3, BUTTON_0};

struct gui_element *result_textfield = NULL;

GUI_EVENT_HANDLER_RESPONSE numerical_button_event_handler(struct gui_event *gui_event)
{
    if (gui_event->type != GUI_EVENT_TYPE_ELEMENT_CLICKED)
    {
        return GUI_EVENT_HANDLER_RESPONSE_IGNORED;
    }
    BUTTON_ID btn_id = gui_event->element.id;
    char c = *itoa(btn_id);
    gui_element_textfield_put_char(result_textfield, c);

    return GUI_EVENT_HANDLER_RESPONSE_PROCESSED_CONTINUE_WITH_CHILDREN;
}

void calculator_add()
{
    gui_element_textfield_put_char(result_textfield, '+');
}

void calculator_subtract()
{
    gui_element_textfield_put_char(result_textfield, '-');
}

void calculator_divide()
{
    gui_element_textfield_put_char(result_textfield, '/');
}

void calculator_multiply()
{
    gui_element_textfield_put_char(result_textfield, '*');
}

void calculator_reset()
{
    gui_element_textfield_text_set(result_textfield, "");
}

bool calculator_is_operator(char c)
{
    return c == '+' || c == '-' || c == '/' || c == '*';
}

int calculator_do_math(int left, int right, char op)
{
    int result = 0;
    switch (op)
    {
    case '+':
        result = left + right;
        break;
    case '-':
        result = left - right;
        break;

    case '*':
        result = left * right;
        break;

    case '/':
        if (right == 0)
        {
            result = 0;
            break;
        }
        result = left / right;
        break;
    }

    return result;
}

void calculator_equals()
{
    // We don't obey BODMAS/operator precedence yet — this app mainly
    // demonstrates the GUI toolkit with all its elements wired up.
    const char *result_text = gui_element_textfield_text(result_textfield);
    const char *current_c = result_text;
    int parse_state = CALCULATOR_PARSING_LEFT_NUMBER;
    char number_str[124] = {0};
    int number_str_index = 0;
    int left_number_int = 0;
    char current_op = 0;
    int result = 0;

    while (*current_c)
    {
        if (calculator_is_operator(*current_c))
        {
            if (parse_state == CALCULATOR_PARSING_RIGHT_NUMBER)
            {
                int right_number_int = atoi(number_str);
                result = calculator_do_math(left_number_int, right_number_int, current_op);

                // Nested expressions: left operand becomes the running result
                left_number_int = result;
                right_number_int = 0;

                // Parse state never goes back to the left number — this
                // implementation treats the expression as continuous
                // (5+5+10+20+30), always operating on the left side.
            }
            else
            {
                left_number_int = atoi(number_str);
                parse_state = CALCULATOR_PARSING_RIGHT_NUMBER;
            }

            current_op = *current_c;
            current_c++;
            memset(number_str, 0x00, sizeof(number_str));
            number_str_index = 0;
            continue;
        }
        number_str[number_str_index] = *current_c;
        current_c++;
        number_str_index++;
    }

    if (current_op != 0)
    {
        int right_number_int = atoi(number_str);
        result = calculator_do_math(left_number_int, right_number_int, current_op);
    }
    else
    {
        // e.g. they entered 5000 without ever using an operator
        result = atoi(number_str);
    }

    const char *result_str = (const char *)itoa(result);
    gui_element_textfield_text_set(result_textfield, result_str);
}

GUI_EVENT_HANDLER_RESPONSE calculator_delete_button_event_handler(struct gui_event *gui_event)
{
    if (gui_event->type != GUI_EVENT_TYPE_ELEMENT_CLICKED)
    {
        return GUI_EVENT_HANDLER_RESPONSE_IGNORED;
    }

    calculator_reset();
    return GUI_EVENT_HANDLER_RESPONSE_PROCESSED_CONTINUE_WITH_CHILDREN;
}

GUI_EVENT_HANDLER_RESPONSE mathimatical_operation_button_event_handler(struct gui_event *gui_event)
{
    if (gui_event->type != GUI_EVENT_TYPE_ELEMENT_CLICKED)
        return GUI_EVENT_HANDLER_RESPONSE_IGNORED;

    BUTTON_ID btn_id = gui_event->element.id;
    switch (btn_id)
    {
    case BUTTON_ADD:
        calculator_add();
        break;

    case BUTTON_SUBTRACT:
        calculator_subtract();
        break;

    case BUTTON_MULTIPLY:
        calculator_multiply();
        break;

    case BUTTON_DIVIDE:
        calculator_divide();
        break;

    case BUTTON_EQUALS:
        calculator_equals();
        break;
    }

    return GUI_EVENT_HANDLER_RESPONSE_PROCESSED_CONTINUE_WITH_CHILDREN;
}

int main(int argc, char **argv)
{
    // guilib needs these ready before any window/element creation
    graphics_image_formats_init();
    font_system_init();

    struct window *main_win = window_create("Calculator", 400, 500, 0, 555);
    if (!main_win)
    {
        return -1;
    }
    window_set_to_receive_stdout(main_win);

    struct gui *gui = gui_bind_to_window(main_win, NULL);
    if (!gui)
    {
        printf("GUI initialization issue\n");
        return -1;
    }

    int padding = 10;
    int textfield_height = 100;
    int button_height = 50;

    int textfield_width = main_win->width - (padding * 2);
    if (textfield_width <= 0)
    {
        printf("Window width too small for calculator\n");
        return -1;
    }

    result_textfield = gui_element_textfield_create(gui, NULL, padding, padding, textfield_width, textfield_height, RESULT_TEXTFIELD);
    if (!result_textfield)
    {
        printf("Failed to create calculator display\n");
        return -1;
    }
    gui_element_textfield_text_set(result_textfield, "");

    size_t buttons_per_row = 3;
    int number_button_width = (main_win->width - (padding * (buttons_per_row + 1))) / buttons_per_row;
    if (number_button_width <= 0)
    {
        printf("Window width too small for calculator buttons\n");
        return -1;
    }

    size_t btn_y_start = result_textfield->y + result_textfield->height + padding;
    size_t total_buttons = sizeof(button_ids) / sizeof(BUTTON_ID);
    for (size_t i = 0; i < total_buttons; i++)
    {
        BUTTON_ID btn_id = button_ids[i];
        size_t row = i / buttons_per_row;
        size_t col = i % buttons_per_row;

        int btn_x = padding + col * (number_button_width + padding);
        int btn_y = btn_y_start + row * (button_height + padding);

        char btn_val[3] = {0};
        sprintf(btn_val, "%i", btn_id);

        struct gui_element *btn_elem = gui_element_button_create(gui, NULL, btn_x, btn_y, number_button_width, button_height, btn_val, btn_id);
        if (!btn_elem)
        {
            printf("Button creation failed\n");
            return -1;
        }

        gui_element_event_handler_set(btn_elem, numerical_button_event_handler);
    }

    size_t rows = (total_buttons + buttons_per_row - 1) / buttons_per_row;
    size_t delete_button_row = rows ? rows - 1 : 0;
    size_t delete_button_col = 1;

    int delete_btn_x = padding + delete_button_col * (number_button_width + padding);
    int delete_btn_y = btn_y_start + delete_button_row * (button_height + padding);
    struct gui_element *delete_btn = gui_element_button_create(gui, NULL, delete_btn_x, delete_btn_y, number_button_width, button_height, "Del", BUTTON_DELETE);
    if (!delete_btn)
    {
        printf("Failed to create delete button\n");
        return -1;
    }
    gui_element_event_handler_set(delete_btn, calculator_delete_button_event_handler);

    size_t grid_height = rows * button_height + (rows ? (rows - 1) * padding : 0);
    int operations_y = btn_y_start + grid_height + padding;

    size_t operations_per_row = 5;
    int operations_button_width = (main_win->width - (padding * (operations_per_row + 1))) / operations_per_row;
    if (operations_button_width <= 0)
    {
        printf("Window width too small for operation buttons\n");
        return -1;
    }

    int op_x = padding;
    struct gui_element *addition_btn = gui_element_button_create(gui, NULL, op_x, operations_y, operations_button_width, button_height, "+", BUTTON_ADD);
    if (!addition_btn)
    {
        printf("Failed to create addition button\n");
        return -1;
    }
    gui_element_event_handler_set(addition_btn, mathimatical_operation_button_event_handler);

    op_x += operations_button_width + padding;
    struct gui_element *subtraction_btn = gui_element_button_create(gui, NULL, op_x, operations_y, operations_button_width, button_height, "-", BUTTON_SUBTRACT);
    if (!subtraction_btn)
    {
        printf("Failed to create subtraction button\n");
        return -1;
    }
    gui_element_event_handler_set(subtraction_btn, mathimatical_operation_button_event_handler);

    op_x += operations_button_width + padding;
    struct gui_element *multiplication_btn = gui_element_button_create(gui, NULL, op_x, operations_y, operations_button_width, button_height, "*", BUTTON_MULTIPLY);
    if (!multiplication_btn)
    {
        printf("Failed to create multiplication button\n");
        return -1;
    }
    gui_element_event_handler_set(multiplication_btn, mathimatical_operation_button_event_handler);

    op_x += operations_button_width + padding;
    struct gui_element *division_btn = gui_element_button_create(gui, NULL, op_x, operations_y, operations_button_width, button_height, "/", BUTTON_DIVIDE);
    if (!division_btn)
    {
        printf("Failed to create division button\n");
        return -1;
    }
    gui_element_event_handler_set(division_btn, mathimatical_operation_button_event_handler);

    op_x += operations_button_width + padding;
    struct gui_element *equals_btn = gui_element_button_create(gui, NULL, op_x, operations_y, operations_button_width, button_height, "=", BUTTON_EQUALS);
    if (!equals_btn)
    {
        printf("Failed to create equals button\n");
        return -1;
    }
    gui_element_event_handler_set(equals_btn, mathimatical_operation_button_event_handler);

    while (gui_process(gui) >= 0)
    {
        usleep(10);
    }

    return 0;
}
