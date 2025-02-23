#include <unistd.h>
#include <fcntl.h>

#define BUFFER_SIZE 4096                  // певвоначальный размер для получения данных
#define MAX_STATE_NAME 50                 // максимальное количество символов для названия состояния
#define MEMORY_POOL_SIZE 32 * 1024 * 1024 // размер блока памяти выделенного на куче
#define INITIAL_TAPE_SIZE 4096            // первоначальный размер ленты
#define GROWTH_FACTOR 2                   // коэфициент для расширения ленты
#define HASH_TABLE_SIZE 512               // размер хэш таблицы, ограничевает количество состояний

char *tape = NULL;                  // лента
int tape_size = INITIAL_TAPE_SIZE;  // размер ленты
int tape_head = 0;                  // индекс позиции на ленте
int transition_index = 0;           // индекс перехода
int memory_offset = 0;              // сдвиг в блоке памяти
char memory_pool[MEMORY_POOL_SIZE]; // выделенная память на куче

int readFileName(char **fileName);                               // считывание имени файла
void *custom_malloc(int size);                                   // выделение памяти
int my_strcmp(const char *a, const char *b);                     // сравнение строк
void expand_tape();                                              // расширение ленты
void readTape();                                                 // считывание ленты
void runMachine();                                               // запуск машины
void parseMachineFile(const char *filename);                     // обработка файла машины тьюринга
void printTape();                                                // вывод ленты
char *my_strcpy(char *dest, const char *src);                    // копирование строк
unsigned int hash_function(const char *state, char read_symbol); // хэш функция

typedef struct
{
    char start_state[MAX_STATE_NAME];
    char read_symbol;
    char next_state[MAX_STATE_NAME];
    char write_symbol;
    char direction;
} Transition;

Transition transitions[HASH_TABLE_SIZE]; // переходы

typedef struct HashNode
{
    Transition *transition;
    struct HashNode *next;
} HashNode; // хэш таблица, хранит связи между переходами(как линкед лист) и хэширует

HashNode *hash_table[HASH_TABLE_SIZE];

void *custom_malloc(int size)
{
    if (memory_offset + size > MEMORY_POOL_SIZE)
    {
        write(2, "Out of memory\n", 14);
        return NULL;
    }

    void *ptr = &memory_pool[memory_offset]; // указатель на начало нового блока
    memory_offset += size;

    return ptr;
}

unsigned int hash_function(const char *state, char read_symbol)
{
    unsigned int hash = 0;

    while (*state)
    {
        hash = (hash * 31) + (*state++);
    }
    hash += read_symbol;

    // логическое И полученного хэша и размера таблицы -1(даст остаток от деления)
    return hash & (HASH_TABLE_SIZE - 1);
}

void add_to_hash_table(Transition *transition)
{
    unsigned int hash = hash_function(transition->start_state, transition->read_symbol);
    //тк машина детерменирована, то пара стартовое состояние и символ уникальны
    HashNode *node = (HashNode *)custom_malloc(sizeof(HashNode));
    node->transition = transition;
    node->next = hash_table[hash]; //если на этом индексе уже есть другие элементы (из-за коллизий), они связываются в цепочку с помощью указателя next
    hash_table[hash] = node;
}

Transition *get_transition_from_hash(const char *current_state, char read_symbol)
{
    unsigned int hash = hash_function(current_state, read_symbol);
    HashNode *node = hash_table[hash]; 
    while (node) //для обработки коллизий
    {
        if (my_strcmp(node->transition->start_state, current_state) == 0 &&
            node->transition->read_symbol == read_symbol)
        {
            return node->transition;
        }
        node = node->next;
    }
    return NULL;
}

int my_strcmp(const char *a, const char *b)
{
    while (*a && (*a == *b))
    {
        a++;
        b++;
    }
    return *(unsigned char *)a - *(unsigned char *)b;
}

char *my_strcpy(char *dest, const char *src) //из src в dest
{
    char *d = dest;
    while ((*d++ = *src++) != '\0');
    return dest;
}

void printTape()
{
    // печатаем все символы ленты
    for (int i = 0; i < tape_size; i++)
    {
        if (tape[i] != '_') // не печатаем символ '_'
        {
            write(1, &tape[i], 1);
        }
    }
    write(1, "\n", 1);
}

int readFileName(char **fileName) 
{
    int bufferSize = 256;
    *fileName = (char *)custom_malloc(bufferSize);
    int index = 0;
    char ch;

    while (read(STDIN_FILENO, &ch, 1) > 0) // считывать один символ за раз из стандартного ввода
    {
        if (ch == '\n')
        {
            break;
        }
        if (index >= bufferSize - 1)
        {
            bufferSize *= GROWTH_FACTOR;
            char *newBuffer = (char *)custom_malloc(bufferSize);
            newBuffer = my_strcpy(*fileName, newBuffer);
            *fileName = newBuffer;
        }

        (*fileName)[index++] = ch;
    }

    (*fileName)[index] = '\0';
    return 0;
}

void parseMachineFile(const char *filename)
{
    int fd = open(filename, O_RDONLY); //флаг файл должен быть открыт только для чтения.

    int buffer_size = BUFFER_SIZE;
    char *buffer = (char *)custom_malloc(buffer_size);
    char *line = (char *)custom_malloc(buffer_size);
    int bytesRead = 0, buffer_index = 0, line_index = 0;
    int currentLine = 0;

    if (!buffer || !line)
    {
        write(2, "Failed to allocate memory\n", 26); //Стандартный поток ошибок (stderr)
        close(fd);
        return;
    }

    while ((bytesRead = read(fd, buffer + buffer_index, buffer_size - buffer_index)) > 0)
    {
        buffer_index += bytesRead;

        while (buffer_index > 0)
        {
            int i = 0;
            // чтение строки до первого символа новой строки
            while (i < buffer_index && buffer[i] != '\n')
            {
                line[line_index++] = buffer[i++];
            }

            if (i < buffer_index && buffer[i] == '\n')
            {
                buffer_index -= i + 1; // Сдвигаем оставшиеся данные в буфере

                for (int j = 0; j < buffer_index; j++)
                {
                    buffer[j] = buffer[i + 1 + j];
                }
                line[line_index] = '\0'; // Завершаем строку

                if (currentLine > 2)
                {
                    char start_state[MAX_STATE_NAME], next_state[MAX_STATE_NAME];
                    char read_symbol, write_symbol, direction;
                    int i = 1;//пропустили скобку
                    int idx = 0;

                    // Парсим строку
                    while (line[i] != ',')
                    {
                        start_state[idx++] = line[i++];
                    }
                    start_state[idx] = '\0';
                    i += 2; //пропустили пробел и запятую
                    read_symbol = line[i];

                    i += 7; //пропустили стрелки скобки
                    idx = 0;
                    while (line[i] != ',')
                    {
                        next_state[idx++] = line[i++];
                    }
                    next_state[idx] = '\0';
                    i += 2;
                    write_symbol = line[i];
                    i += 3;
                    direction = line[i];
                    Transition t;
                    my_strcpy(t.start_state, start_state);
                    t.read_symbol = read_symbol;
                    my_strcpy(t.next_state, next_state);
                    t.write_symbol = write_symbol;
                    t.direction = direction;

                    transitions[transition_index++] = t; //занесли
                }

                line_index = 0;
                currentLine++;
            }
            else if (i == buffer_index) // Если нет новой строки, расширяем буфер
            {
                if (buffer_index == buffer_size)
                {
                    buffer_size *= GROWTH_FACTOR;
                    char *new_buffer = (char *)custom_malloc(buffer_size);
                    if (!buffer)
                    {
                        write(2, "Failed to expand buffer\n", 25);
                        close(fd);
                        return;
                    }
                }
                break; 
            }
        }
    }

    //Обработка всех переходов в хеш-таблицу
    for (int i = 0; i < transition_index; i++)
    {
        add_to_hash_table(&transitions[i]);
    }

    close(fd);
}

void runMachine()
{
    char current_state[MAX_STATE_NAME] = "start";
    int step_count = 0; // Счётчик шагов

    while (my_strcmp(current_state, "stop") != 0) //пока не дошли до конечного
    {
        if (step_count >= 100000)
        {
            write(1, "Exceeded maximum step count\n", 28);
            break;
        }

        if (tape_head >= tape_size || tape_head < 0)
        {
            expand_tape();
        }

        char read_symbol = tape[tape_head];

        Transition *t = get_transition_from_hash(current_state, read_symbol);
        if (!t)
        {
            write(1, "No transition\n", 15);
            break;
        }

        tape[tape_head] = t->write_symbol;

        my_strcpy(current_state, t->next_state);
        if (t->direction == '>')
        {
            tape_head++;
        }
        else if (t->direction == '<')
        {
            tape_head--;
        }

        step_count++; // Увеличиваем счётчик шагов
    }
    write(1, "Final tape: ", 13);
    printTape();
}

void readTape()
{
    int buffer_size = 256;
    char *buffer = (char *)custom_malloc(buffer_size);
    if (!buffer)
    {
        write(2, "Failed to allocate buffer\n", 26);
        return;
    }

    int index = 0;
    char ch;

    while (read(STDIN_FILENO, &ch, 1) > 0)
    {
        if (ch == '\n')
        {
            break;
        }
        if (index >= buffer_size - 1)
        {
            buffer_size *= 2;
            char *new_buffer = (char *)custom_malloc(buffer_size);
            if (!new_buffer)
            {
                write(2, "Failed to expand buffer\n", 25);
                return;
            }
            my_strcpy(new_buffer, buffer);
            buffer = new_buffer;
        }
        buffer[index++] = ch;
    }
    buffer[index] = '\0';

    tape = (char *)custom_malloc(buffer_size); //сколько надо было для буфера столько и берем на ленту
    if (!tape)
    {
        write(2, "Failed to allocate tape\n", 24);
        return;
    }

    for (int i = 0; i < tape_size; i++)
    {
        tape[i] = '_';
    }

    int start_pos = (tape_size - index) / 2; //возьмем в центре
    for (int i = 0; i < index; i++)
    {
        if (buffer[i] == ' ')
        {
            tape[start_pos + i] = '_';
        }
        else
        {
            tape[start_pos + i] = buffer[i];
        }
    }
    tape_head = start_pos;
}

void expand_tape()
{
    int new_size = tape_size * GROWTH_FACTOR;
    char *new_tape = (char *)custom_malloc(new_size);
    if (!new_tape)
    {
        write(2, "Failed to expand tape\n", 23);
        return;
    }
    for (int i = 0; i < new_size; i++)
    {
        new_tape[i] = '_';
    }
    int left_offset = (new_size - tape_size) / 2;
    int right_offset = new_size - tape_size - left_offset;

    for (int i = 0; i < tape_size; i++)
    {
        new_tape[left_offset + i] = tape[i];
    }

    tape = new_tape;
    tape_size = new_size;
    tape_head += left_offset;
}

int main()
{
    char *filename;

    if (readFileName(&filename) != 0)
    {
        write(2, "Error reading filename\n", 23);
        return -1;
    }
    parseMachineFile(filename);
    readTape();

    runMachine();

    return 0;
}