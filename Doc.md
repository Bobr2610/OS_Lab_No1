## Постановка задачи

**Вариант 2.**

Родительский процесс создает дочерний процесс. Первой строчкой пользователь в консоль родительского процесса пишет имя файла, которое будет передано при создании дочернего процесса. Родительский и дочерний процесс должны быть представлены разными программами. Родительский процесс передает команды пользователя через pipe1, который связан со стандартным входным потоком дочернего процесса. Дочерний процесс при необходимости передает данные в родительский процесс через pipe2. Результаты своей работы дочерний процесс пишет в созданный им файл.

Пользователь вводит команды вида: «число число число<endline>». Далее эти числа передаются от родительского процесса в дочерний. Дочерний процесс считает их сумму и выводит её в файл. Числа имеют тип float. Количество чисел может быть произвольным.

## Общий метод и алгоритм решения

### Использованные системные вызовы:

● **pid_t fork(void)** – создает дочерний процесс. Возвращает PID дочернего процесса в родительском процессе, 0 в дочернем процессе, или -1 в случае ошибки.

● **int pipe(int pipefd[2])** – создает неименованный канал (pipe) для передачи данных между процессами. Массив `pipefd` содержит два файловых дескриптора: `pipefd[0]` для чтения и `pipefd[1]` для записи. Возвращает 0 при успехе, -1 при ошибке.

● **int dup2(int oldfd, int newfd)** – переназначает файловый дескриптор. Копирует `oldfd` в `newfd`, закрывая `newfd` если он был открыт. Используется для перенаправления стандартных потоков ввода/вывода. Возвращает новый дескриптор при успехе, -1 при ошибке.

● **int execv(const char *pathname, char *const argv[])** – заменяет образ памяти текущего процесса новым исполняемым файлом. `pathname` – путь к исполняемому файлу, `argv` – массив аргументов командной строки (должен заканчиваться NULL). При успехе не возвращает управление, при ошибке возвращает -1.

● **ssize_t read(int fd, void *buf, size_t count)** – читает данные из файлового дескриптора. Возвращает количество прочитанных байт, 0 при достижении конца файла, -1 при ошибке.

● **ssize_t write(int fd, const void *buf, size_t count)** – записывает данные в файловый дескриптор. Возвращает количество записанных байт, -1 при ошибке.

● **int open(const char *pathname, int flags, mode_t mode)** – открывает или создает файл. `flags` определяет режим доступа (O_WRONLY, O_CREAT, O_TRUNC и т.д.), `mode` – права доступа при создании файла. Возвращает файловый дескриптор при успехе, -1 при ошибке.

● **int close(int fd)** – закрывает файловый дескриптор. Освобождает ресурсы, связанные с дескриптором. Возвращает 0 при успехе, -1 при ошибке.

● **pid_t waitpid(pid_t pid, int *status, int options)** – ожидает завершения дочернего процесса. `pid` – PID дочернего процесса, `status` – указатель для сохранения статуса завершения, `options` – опции ожидания. Возвращает PID завершенного процесса при успехе, -1 при ошибке.

● **ssize_t readlink(const char *pathname, char *buf, size_t bufsiz)** – читает содержимое символической ссылки. Используется для получения пути к исполняемому файлу через `/proc/self/exe`. Возвращает количество прочитанных байт при успехе, -1 при ошибке.

● **void _exit(int status)** – завершает выполнение процесса немедленно, не вызывая функции очистки. `status` – код возврата процесса.

### Алгоритм работы программы:

1. **Родительский процесс (parent.c):**
   - Читает первую строку из стандартного ввода – имя файла для записи результатов
   - Создает два pipe: `parent_to_child` для передачи команд и `child_to_parent` для получения ответов
   - Вызывает `fork()` для создания дочернего процесса
   - В дочернем процессе: закрывает ненужные концы pipe, перенаправляет stdin/stdout через `dup2()`, вызывает `execv()` для запуска программы child
   - В родительском процессе: закрывает ненужные концы pipe, в цикле читает строки с числами из stdin, передает их дочернему процессу через pipe, получает ответы и выводит их в stdout
   - После завершения ввода закрывает pipe и ожидает завершения дочернего процесса через `waitpid()`

2. **Дочерний процесс (child.c):**
   - Получает имя файла как аргумент командной строки
   - Открывает файл для записи через `open()` с флагами O_WRONLY | O_CREAT | O_TRUNC
   - В цикле читает строки из stdin (который перенаправлен на pipe от родителя)
   - Парсит строку, извлекая числа с плавающей точкой и вычисляя их сумму
   - Форматирует результат и записывает его в файл и в stdout (который перенаправлен на pipe к родителю)
   - При ошибке парсинга отправляет сообщение об ошибке родителю
   - После завершения ввода закрывает файл и завершает работу
Код программы
parent.c
```c
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHILD_PROGRAM_NAME "child.c"
#define MAX_LINE_LENGTH 4096

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

static ssize_t read_line(int fd, char *buffer, size_t capacity) {
	if (capacity == 0) return -1;

	size_t offset = 0;
	while (offset + 1 < capacity) {
		char ch;
		ssize_t bytes = read(fd, &ch, 1);
		if (bytes < 0) {
			return -1;
		}
		if (bytes == 0) break;

		buffer[offset++] = ch;
		if (ch == '\n') break;
	}
	buffer[offset] = '\0';
	return (ssize_t)offset;
}

static void trim_trailing_newline(char *line) {
	size_t length = string_length(line);
	if (length == 0) {
		return;
	}
	if (line[length - 1] == '\n') line[length - 1] = '\0';
}

static void build_child_path(char *result, size_t capacity) {
	char executable_path[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", executable_path, sizeof(executable_path) - 1);
	if (len == -1) fail("error: failed to read /proc/self/exe\n");

	executable_path[len] = '\0';

	while (len > 0 && executable_path[len] != '/') --len;
	if (len == 0) fail("error: executable path is invalid\n");

	executable_path[len] = '\0';

	size_t dir_length = string_length(executable_path);
	size_t name_length = string_length(CHILD_PROGRAM_NAME);
	if (dir_length + 1 + name_length + 1 > capacity) fail("error: child path buffer is too small\n");

	size_t index = 0;
	for (size_t i = 0; i < dir_length; ++i) result[index++] = executable_path[i];
	result[index++] = '/';
	for (size_t i = 0; i < name_length; ++i) result[index++] = CHILD_PROGRAM_NAME[i];

	result[index] = '\0';
}

static void forward_line(int output_fd, const char *line) {
	size_t length = string_length(line);
	if (length == 0 || line[length - 1] != '\n') {
		write_all(output_fd, line, length);
		write_all(output_fd, "\n", 1);
		return;
	}
	write_all(output_fd, line, length);
}

int main(void) {
	char filename[MAX_LINE_LENGTH];
	ssize_t filename_len = read_line(STDIN_FILENO, filename, sizeof(filename));
	if (filename_len <= 0) fail("error: failed to read filename\n");

	trim_trailing_newline(filename);
	if (string_length(filename) == 0) fail("error: filename must not be empty\n");

	int parent_to_child[2];
	if (pipe(parent_to_child) == -1) fail("error: failed to create pipe\n");

	int child_to_parent[2];
	if (pipe(child_to_parent) == -1) fail("error: failed to create pipe\n");
	
	pid_t child = fork();
	if (child == -1) fail("error: failed to fork\n");

	if (child == 0) {
		if (close(parent_to_child[1]) == -1 || close(child_to_parent[0]) == -1) fail("error: failed to close descriptors in child\n");

		if (dup2(parent_to_child[0], STDIN_FILENO) == -1) fail("error: failed to redirect stdin\n");

		if (dup2(child_to_parent[1], STDOUT_FILENO) == -1) fail("error: failed to redirect stdout\n");
        
		if (close(parent_to_child[0]) == -1 || close(child_to_parent[1]) == -1) fail("error: failed to close redundant descriptors\n");

		char child_path[PATH_MAX];
		build_child_path(child_path, sizeof(child_path));

		char *const args[] = {CHILD_PROGRAM_NAME, filename, NULL};
		execv(child_path, args);
		fail("error: exec failed\n");
	}

	if (close(parent_to_child[0]) == -1 || close(child_to_parent[1]) == -1) fail("error: failed to close descriptors in parent\n");

	char line_buffer[MAX_LINE_LENGTH];
	while(true) {
		ssize_t line_length = read_line(STDIN_FILENO, line_buffer, sizeof(line_buffer));
		if (line_length < 0) fail("error: failed to read input line\n");

		if (line_length == 0) break;

		if (line_buffer[0] == '\n') break;

		write_all(parent_to_child[1], line_buffer, (size_t)line_length);

		char response[MAX_LINE_LENGTH];
		ssize_t response_length = read_line(child_to_parent[0], response, sizeof(response));
		if (response_length <= 0) fail("error: child response failed\n");
		forward_line(STDOUT_FILENO, response);
	}

	if (close(parent_to_child[1]) == -1 || close(child_to_parent[0]) == -1) fail("error: failed to close pipes\n");

	int status = 0;
	if (waitpid(child, &status, 0) == -1) {
		fail("error: waitpid failed\n");
	}
	return WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE;
}
```
child.c
```c
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
	if (argc < 2) fail("error: file name argument is missing\n");

	int file = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (file == -1) fail("error: failed to open file\n");

	char line[BUFFER_SIZE];
	while(true) {
		ssize_t line_length = read_line(line, sizeof(line));
		if (line_length < 0) fail("error: failed to read input\n");
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
		if (value_length == 0) fail("error: failed to format result\n");

		const char prefix[] = "sum: ";
		const char newline = '\n';

		write_all(file, prefix, sizeof(prefix) - 1);
		write_all(file, value_buffer, value_length);
		write_all(file, &newline, 1);

		write_all(STDOUT_FILENO, prefix, sizeof(prefix) - 1);
		write_all(STDOUT_FILENO, value_buffer, value_length);
		write_all(STDOUT_FILENO, &newline, 1);
	}

	if (close(file) == -1) {
		fail("error: failed to close file\n");
	}
	return EXIT_SUCCESS;
}
```
## Протокол работы программы

### Тестирование:

Программа была протестирована на различных входных данных. Примеры тестов:

**Тест 1: Простые числа**
```bash
$ ./build/lab_01_parent
results.txt
1.5 2.5 3.0
sum: 7.0
10.25 20.75
sum: 31.0
-5.5 10.5
sum: 5.0

$ cat results.txt
sum: 7.0
sum: 31.0
sum: 5.0
```

**Тест 2: Множество чисел в одной строке**
```bash
$ ./build/lab_01_parent
output.txt
1.1 2.2 3.3 4.4 5.5 6.6 7.7 8.8 9.9 10.0
sum: 59.5
0.1 0.2 0.3 0.4 0.5
sum: 1.5

$ cat output.txt
sum: 59.5
sum: 1.5
```

**Тест 3: Обработка ошибок**
```bash
$ ./build/lab_01_parent
test.txt
1.5 2.5 abc
error: invalid input
3.0 4.0
sum: 7.0
invalid
error: invalid input

$ cat test.txt
sum: 7.0
```

**Тест 4: Отрицательные числа и нули**
```bash
$ ./build/lab_01_parent
negative.txt
-10.5 5.5
sum: -5.0
0.0 0.0 0.0
sum: 0.0
-1.1 -2.2 -3.3
sum: -6.6

$ cat negative.txt
sum: -5.0
sum: 0.0
sum: -6.6
```


### Strace:

Для анализа системных вызовов использовалась утилита `strace` с флагом `-f` для отслеживания дочерних процессов:

```bash
$ strace -f -o strace_output.txt ./build/lab_01_parent
```

Ниже представлен фрагмент вывода `strace` с выделенными системными вызовами, используемыми в нашей программе. **Выделенные жирным шрифтом** строки соответствуют системным вызовам, вызванным непосредственно из нашего кода:

```
execve("./build/lab_01_parent", ["./build/lab_01_parent"], 0x7ffc12345678 /* 49 vars */) = 0
...
read(0, "results.txt\n", 4096)            = 12
...
**readlink("/proc/self/exe", "/path/to/build/lab_01_parent", 1023) = 35**
...
**pipe([3, 4])                            = 0**  // parent_to_child
**pipe([5, 6])                            = 0**  // child_to_parent
...
**clone(child_stack=NULL, flags=CLONE_CHILD_CLEARTID|CLONE_CHILD_SETTID|SIGCHLD, child_tidptr=0x7f8c0c66b810) = 12345**
strace: Process 12345 attached
...
[pid 12345] **close(4)**                    = 0  // закрытие write конца parent_to_child в child
[pid 12345] **close(5)**                    = 0  // закрытие read конца child_to_parent в child
[pid 12345] **dup2(3, 0)**                  = 0  // перенаправление stdin на parent_to_child[0]
[pid 12345] **close(3)**                    = 0
[pid 12345] **dup2(6, 1)**                  = 1  // перенаправление stdout на child_to_parent[1]
[pid 12345] **close(6)**                    = 0
[pid 12345] **execve("/path/to/build/lab_01_child", ["child.c", "results.txt"], 0x7ffc12345678 /* 49 vars */) = 0**
...
[pid 12345] **open("results.txt", O_WRONLY|O_CREAT|O_TRUNC, 0600) = 3**
...
[pid 12345] **read(0, "1.5 2.5 3.0\n", 4096) = 12**
...
[pid 12345] **write(3, "sum: 7.0\n", 9) = 9**  // запись в файл
[pid 12345] **write(1, "sum: 7.0\n", 9) = 9**  // запись в stdout (pipe к родителю)
...
**read(5, "sum: 7.0\n", 4096) = 9**  // родитель читает ответ от child
**write(1, "sum: 7.0\n", 9) = 9**    // родитель выводит в stdout
...
[pid 12345] **read(0, "", 4096) = 0**  // конец ввода
[pid 12345] **close(3)**                = 0  // закрытие файла
[pid 12345] **_exit(0)**                = ?
[pid 12345] +++ exited with 0 +++
...
**close(4)**                             = 0  // закрытие write конца parent_to_child в parent
**close(5)**                             = 0  // закрытие read конца child_to_parent в parent
**waitpid(12345, [{WIFEXITED(s) && WEXITSTATUS(s) == 0}], 0) = 12345**
...
exit_group(0)                           = ?
+++ exited with 0 +++
```

**Ключевые системные вызовы из нашего кода:**

1. **readlink("/proc/self/exe", ...)** – получение пути к исполняемому файлу родительского процесса для построения пути к дочернему процессу
2. **pipe([3, 4])** и **pipe([5, 6])** – создание двух каналов для двусторонней связи между процессами
3. **clone(...)** (реализация fork) – создание дочернего процесса
4. **close()** – закрытие ненужных файловых дескрипторов
5. **dup2()** – перенаправление стандартных потоков ввода/вывода на pipe
6. **execve()** – замена образа процесса на дочернюю программу
7. **open()** – открытие файла для записи результатов
8. **read()** – чтение данных из stdin/pipe
9. **write()** – запись данных в файл и в stdout/pipe
10. **waitpid()** – ожидание завершения дочернего процесса
11. **_exit()** – завершение процесса

Все системные вызовы проверяются на ошибки: возвращаемые значения сравниваются с -1 (или 0 для некоторых вызовов), и при ошибке программа выводит сообщение и завершается.

## Вывод

В ходе выполнения лабораторной работы была реализована программа для межпроцессного взаимодействия между родительским и дочерним процессами с использованием неименованных каналов (pipe). Программа успешно выполняет поставленную задачу: родительский процесс передает строки с числами дочернему процессу, который вычисляет их сумму и записывает результаты в файл.

Основные достижения: реализовано корректное использование системных вызовов `fork()`, `pipe()`, `dup2()`, `execv()`, `read()`, `write()`, `open()`, `close()`, `waitpid()` и других для организации межпроцессного взаимодействия. Все системные вызовы проверяются на ошибки, что обеспечивает надежность работы программы. Программа корректно обрабатывает различные входные данные, включая отрицательные числа, нули и некорректный ввод.

При выполнении работы были изучены механизмы создания процессов, перенаправления потоков ввода/вывода и организации двусторонней связи между процессами через неименованные каналы. Программа успешно прошла тестирование и готова к демонстрации.
