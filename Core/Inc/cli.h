/*
 * Command shell. Type "help" on the console for the command list.
 * Output is line based and stable so tools/pulse_test.py can parse it.
 */
#ifndef CLI_H
#define CLI_H

void cli_prompt(void);
void cli_execute(char *line);

#endif /* CLI_H */
