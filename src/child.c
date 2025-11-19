#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE 4096

static size_t string_length(const char *text) {
	size_t length = 0;
	while (text[length] != '\0') ++length;
	return length;
}

static void write_all(int fd, const char *buffer, size_t length) {
	while (length > 0) {
		ssize_t written = write(fd, buffer, length);
		if (written < 0) _exit(EXIT_FAILURE);

		buffer += (size_t)written;
		length -= (size_t)written;
	}
}

static void fail(const char *message) {
	write_all(STDERR_FILENO, message, string_length(message));
	_exit(EXIT_FAILURE);
}

static ssize_t read_line(char *buffer, size_t capacity) {
	if (capacity == 0) return -1;

	size_t offset = 0;
	while (offset + 1 < capacity) {
		char ch;
		ssize_t bytes = read(STDIN_FILENO, &ch, 1);
		if (bytes < 0) return -1;

		if (bytes == 0) break;

		buffer[offset++] = ch;
		if (ch == '\n') break;
	}
	buffer[offset] = '\0';
	return (ssize_t)offset;
}

static bool parse_and_sum(char *line, double *result) {
	char *cursor = line;
	double total = 0.0;
	bool has_value = false;
	while (*cursor != '\0') {
		while (*cursor == ' ' || *cursor == '\t') ++cursor;
		
		if (*cursor == '\0') break;
		errno = 0;
		char *next = NULL;
		// string -> double
		double value = strtod(cursor, &next);
		if (cursor == next || errno == ERANGE) return false;

		total += value;
		has_value = true;
		cursor = next;
	}
	if (!has_value) return false;
	*result = total;
	return true;
}
// double -> string
static size_t format_double(double value, char *buffer, size_t capacity) {
	if (capacity == 0) return 0;
	size_t index = 0;
	if (value < 0.0) {
		buffer[index++] = '-';
		value = -value;
		if (index >= capacity) return 0;
	}

	long long integer_part = (long long)value;
	double fractional_part = value - (double)integer_part;

	char digits[32];
	size_t digit_count = 0;
	do {
		digits[digit_count++] = (char)('0' + (integer_part % 10));
		integer_part /= 10;
	} while (integer_part > 0 && digit_count < sizeof(digits));

	while (digit_count > 0 && index < capacity) {
		buffer[index++] = digits[--digit_count];
	}
	if (index >= capacity) {
		return 0;
	}

	buffer[index++] = '.';
	for (int i = 0; i < 6 && index < capacity; ++i) {
		fractional_part *= 10.0;
		int digit = (int)fractional_part;
		buffer[index++] = (char)('0' + digit);
		fractional_part -= digit;
	}

	size_t end = index;
	while (end > 0 && buffer[end - 1] == '0') {
		--end;
	}
	if (end > 0 && buffer[end - 1] == '.') ++end;

	if (end >= capacity) return 0;
	buffer[end] = '\0';
	return end;
}

int main(int argc, char **argv) {
	// check how many arg 
	if (argc < 2) {
		fail("error: file name argument is missing\n");
	}
	// O_WRONLY - write only, O_CREAT - create if not exists, O_TRUNC - truncate if exists, 0600 - R & W
	int file = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (file == -1) {
		fail("error: failed to open file\n");
	}

	char line[BUFFER_SIZE];
	while(true) {
		ssize_t line_length = read_line(line, sizeof(line));
		if (line_length == -1){
			fail("error: failed to read input\n");
		}
		if (line_length == 0) break;
		if (line[line_length - 1] == '\n')line[line_length - 1] = '\0';
		double sum = 0.0;
		if (!parse_and_sum(line, &sum)) {
			const char warning[] = "error: invalid input\n";
			write_all(STDOUT_FILENO, warning, sizeof(warning) - 1);
			continue;
		}

		char value_buffer[128];
		size_t value_length = format_double(sum, value_buffer, sizeof(value_buffer));
		if (value_length == 0) {
			fail("error: failed to format result\n");
		}

		const char prefix[] = "sum: ";
		const char newline = '\n';
		// write to file
		write_all(file, prefix, sizeof(prefix) - 1);
		write_all(file, value_buffer, value_length);
		write_all(file, &newline, 1);
		// write to ter	
		write_all(STDOUT_FILENO, prefix, sizeof(prefix) - 1);
		write_all(STDOUT_FILENO, value_buffer, value_length);
		write_all(STDOUT_FILENO, &newline, 1);
	}

	if (close(file) == -1) {
		fail("error: failed to close file\n");
	}
	return EXIT_SUCCESS;
}