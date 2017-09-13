#include <EEPROM.h>
#include <avr/pgmspace.h>

// Размеры дисков
#define EEPROM_DISK_SIZE (EEPROM.length())
#define RAM_DISK_SIZE 1024
#define ROM_DISK_SIZE strlen(rom_disk)

// Размеры буферов
#define COMMAND_BUFFER_SIZE 40
#define FILENAME_BUFFER_SIZE 40
#define STRING_BUFFER_SIZE 50

// Коды дисков
#define EEPROM_DISK 1
#define RAM_DISK 2
#define ROM_DISK 3

// Коды ошибок
#define FILE_NOT_FOUND -1
#define FILE_TOO_BIG -2
#define FILE_NAME_ERROR -3
#define FILE_OK 1

// Строки
#define COMMAND_PROMPT F(":>")
#define INPUT_TEXT_PROMPT F("Text (Ctrl+C to end): ")
#define VERSION_STRING F("Arduino DOS ver 1.7")
#define HELP_STRING F("Commands: ver dir help show append rewrite del")
#define ERROR_FILE_NOT_FOUND F("File not found")
#define ERROR_FILE_NAME_ERROR F("File name error")
#define ERROR_FILE_TOO_BIG F("File too big")
#define OPERATION_COMPLETE F("Done")
#define UNKNOWN_COMMAND F("Unknown command")
#define TOTAL_SPACE F("Total space: ")
#define USED_SPACE F("Used space: ")
#define FREE_SPACE F("Free space: ")

#define UNIT_BYTES F(" b")

#define UNIT_DAYS F(" d")
#define UNIT_HOURS F(" h")
#define UNIT_MINUTES F(" m")
#define UNIT_SECONDS F(" s")
#define UNIT_MILLISECONDS F(" ms")
#define UNIT_MICROSECONDS F(" mcs")

#define FILE_LIST_PREFIX F("Files:")
#define FILE_NAME_PREFIX F("   ")
#define EEPROM_DISK_DESC F(": is a EEPROM disk")
#define RAM_DISK_DESC F(": is a RAM disk")
#define ROM_DISK_DESC F(": is a ROM disk")
#define EEPROM_DISK_NAME F("C")
#define RAM_DISK_NAME F("A")
#define ROM_DISK_NAME F("B")
#define EMPTY_STRING F("")

// ROM диск
const char PROGMEM rom_disk[]="\
changelog\v\
10.2.2017 16:31 v1.1 added uptime command\n\
10.2.2017 17:31 v1.3 more space on RAM disk\n\
10.2.2017 18:50 v1.4 added ROM disk\n\
10.2.2017 18:50 v1.5 added more ROM disk files\n\
17.2.2017 11:39 v1.6 added utf-8 support to files\n\
17.2.2017 12:11 v1.7 added multi-line input\v\
about\v\
Avtor - Tsarev Vladimir\v\
help\v\
Available commands:\n\
\n\
ver     show current version info\n\
help    show short help\n\
uptime  show uptime\n\
\n\
eeprom: change current disk to eeprom:\n\
ram:    change current disk to ram:\n\
rom:    change current disk to rom:\n\
format  cleanup current disk\n\
dir     show current disk files\n\
\n\
show    show file content\n\
del     delete file\n\
append  add string to end of file (also creates new if not exists)\n\
rewrite erase file and add string\v\
";

// RAM диск
char ram_disk[RAM_DISK_SIZE];


// Режим отладки - когда включен, есть команда dump
#define DEBUG

// Переменные
char current_disk_index;
int current_disk_size;

// =================================================
// Прочитать байт с диска
// =================================================

int dos_read_byte(int index) {
  char b;
  if(index<0 || index>=current_disk_size) return -1;
  switch(current_disk_index) {
    case EEPROM_DISK: return EEPROM.read(index); break;
    case RAM_DISK: return ram_disk[index]; break;
    case ROM_DISK:
      b=pgm_read_byte_near(rom_disk+index);
      if(b=='\v') b=0;
      return b;
      break;
  }
}

// =================================================
// Записать байт на диск
// =================================================

void dos_write_byte(int index,char data) {
  if(index<0 || index>=current_disk_size) return;
  switch(current_disk_index) {
    case EEPROM_DISK: EEPROM.update(index,data); break;
    case RAM_DISK: ram_disk[index]=data; break;
    case ROM_DISK: break;
  }
}

// =================================================
// Выбрать текущий диск
// =================================================

void command_set_disk(int disk_index) {
  switch(disk_index) {
    case EEPROM_DISK:
      current_disk_index=disk_index;
      current_disk_size=EEPROM_DISK_SIZE;
      break;
    case RAM_DISK:
      current_disk_index=disk_index;
      current_disk_size=RAM_DISK_SIZE;
      break;
    case ROM_DISK:
      current_disk_index=disk_index;
      current_disk_size=ROM_DISK_SIZE;
      break;
  }
}

// =================================================
// Очистить текущий диск
// =================================================

void command_format_disk() {
  int i;
  for(i=0;i!=current_disk_size;i++) dos_write_byte(i,0);
}

// =================================================
// Получить первый свободный байт на диске
// =================================================
int get_free_space_index() {
  int i;
  int content_len;
  char name_flag,name_present;
  char current_byte;
  name_flag=1;
  name_present=0;
  for(i=0;i!=current_disk_size;i++) {
    current_byte=dos_read_byte(i);
    if(current_byte==0) {
      // Диск начинающийся с нулевого байта - пустой
      if(i==0) return i;
      // Если сейчас была область имени
      if(name_flag==1) {
        // Если имя отсутствовало - начало свободной области
        if(name_present==0) return i;
        // Если нет, то просто начинается содержимое файла
        name_flag=0;
      } else {
        // Если нулевой байт после содержимого файла - начало имени следующего файла
        name_flag=1;
        name_present=0;
      }
    } else {
      if(name_flag==1) name_present=1;
    }
  }
}

#ifdef DEBUG
// =================================================
// Вывести двоичное содержимое текущего диска
// =================================================

void command_dump() {
  int i;
  unsigned char current_byte;
  char buff[10];
  for(i=0;i!=current_disk_size;i++) {
    if((i%16)==0) {
      if(i>0) {
        Serial.println(EMPTY_STRING);
      }
      sprintf(buff,"0x%03X | ",i);
      Serial.print(buff);
    }
    current_byte=dos_read_byte(i);
    sprintf(buff,"%02X ",current_byte);
    Serial.print(buff);
  }
  Serial.println(EMPTY_STRING);
}
#endif

// =================================================
// Показать букву диска
// =================================================

void show_disk_name() {
  switch(current_disk_index) {
    case EEPROM_DISK: Serial.print(EEPROM_DISK_NAME); break;
    case RAM_DISK: Serial.print(RAM_DISK_NAME); break;
    case ROM_DISK: Serial.print(ROM_DISK_NAME); break;
  }
}

// =================================================
// Показать информацию о диске
// =================================================

void show_disk_info() {
  show_disk_name();
  switch(current_disk_index) {
    case EEPROM_DISK: Serial.print(EEPROM_DISK_DESC); break;
    case RAM_DISK: Serial.print(RAM_DISK_DESC); break;
    case ROM_DISK: Serial.print(ROM_DISK_DESC); break;
  }
  Serial.println(EMPTY_STRING);
}

// =================================================
// Показать список файлов
// =================================================

void command_dir() {
  int i;
  int current_byte;
  char name_flag;
  int used_space;
  char name_present;

  // Показываем информацию о диске
  show_disk_info();

  // Читаем весь диск в поисках файлов
  Serial.println(FILE_LIST_PREFIX);
  name_flag=1;
  name_present=0;
  for(i=0;i!=current_disk_size;i++) {
    current_byte=dos_read_byte(i);
    if(current_byte==0) {
      if(name_flag==0) {
        name_flag=1;
        if(name_present!=0) {
          Serial.println(EMPTY_STRING);
        }
        name_present=0;
      } else {
        name_flag=0;
      }
    } else {
      // Если это имя - выводим имя
      if(name_flag==1) {
        if(name_present==0) Serial.print(FILE_NAME_PREFIX);
        Serial.print((char)current_byte);
        name_present=1;
      }
    }
  }
  // Информация о месте на диске
  Serial.println(EMPTY_STRING);
  Serial.print(TOTAL_SPACE);
  Serial.print(current_disk_size);
  Serial.println(UNIT_BYTES);
  
  // Получаем индекс первого свободного байта
  // Так как байты считаются с 0, то это одновременно количество занятых байт на диске
  // Выводим информацию о месте на диске
  used_space=get_free_space_index();
  Serial.print(USED_SPACE);
  Serial.print(used_space);
  Serial.println(UNIT_BYTES);
  Serial.print(FREE_SPACE);
  Serial.print(current_disk_size-used_space);
  Serial.println(UNIT_BYTES);
  
}

// =================================================
// Показать текст ошибки по коду
// =================================================

void show_decode_result(int result) {
  switch(result) {
    case FILE_OK: Serial.println(OPERATION_COMPLETE); break;
    case FILE_NOT_FOUND: Serial.println(ERROR_FILE_NOT_FOUND); break;
    case FILE_NAME_ERROR: Serial.println(ERROR_FILE_NAME_ERROR); break;
    case FILE_TOO_BIG: Serial.println(ERROR_FILE_TOO_BIG); break;
  }
}

// =================================================
// Проверить имя файла
// =================================================
int check_file_name(char *filename) {
  int i;
  // Если длина имени 0
  if(strlen(filename)==0) return FILE_NAME_ERROR;
  // Если есть пробелы или служебные символы
  for(i=0;i!=strlen(filename);i++) {
    if(!isPrintable(filename[i]) || isspace(filename[i])) return FILE_NAME_ERROR;
  }
  return FILE_OK;
}

// =================================================
// Найти начало имени файла
// =================================================

int find_file_begin(char *filename) {
  int i;
  char current_byte;
  char name_flag;
  char name_buffer[FILENAME_BUFFER_SIZE];
  int name_begin;
  int name_buffer_index;

  // Проверяем корректность имени файла
  if(check_file_name(filename)==FILE_NAME_ERROR) {
    show_decode_result(FILE_NAME_ERROR);
    return FILE_NAME_ERROR;
  }
  
  name_flag=1;
  name_begin=0;
  name_buffer_index=0;
  memset(name_buffer,0,FILENAME_BUFFER_SIZE);
  
  for(i=0;i!=current_disk_size;i++) {
    current_byte=dos_read_byte(i);
    if(current_byte==0) {
      if(name_flag==1) {
        // Если это было имя, и оно закончилось - сверяем
        if(strcmp(filename,name_buffer)==0) {
          return name_begin;
        }
        // Если не совпало - читаем диск дальше
        name_flag=0;
      } else {
        name_flag=1;
        name_begin=i+1;
        name_buffer_index=0;
        memset(name_buffer,0,FILENAME_BUFFER_SIZE);
      }
    } else {
      if(name_flag==1) {
        name_buffer[name_buffer_index]=current_byte;
        name_buffer_index++;
      }
    }
  }
  return FILE_NOT_FOUND;
}

// =================================================
// Найти первый байт содержимого файла
// =================================================

int find_file_content(char *filename) {
  int i;
  int file_begin;
  char current_byte;

  // Проверяем корректность имени файла
  if(check_file_name(filename)==FILE_NAME_ERROR) {
    show_decode_result(FILE_NAME_ERROR);
    return FILE_NAME_ERROR;
  }
  
  file_begin=find_file_begin(filename);
  if(file_begin==FILE_NOT_FOUND) return FILE_NOT_FOUND;
  
  for(i=file_begin;i!=current_disk_size;i++) {
    current_byte=dos_read_byte(i);
    if(current_byte==0) return (i+1);
  }
}

// =================================================
// Найти первый байт после конца файла
// =================================================

int find_file_end(char *filename) {
  int i;
  int file_content;
  char current_byte;

  // Проверяем корректность имени файла
  if(check_file_name(filename)==FILE_NAME_ERROR) {
    show_decode_result(FILE_NAME_ERROR);
    return FILE_NAME_ERROR;
  }
  
  file_content=find_file_content(filename);
  if(file_content==FILE_NOT_FOUND) return FILE_NOT_FOUND;
  for(i=file_content;i!=current_disk_size;i++) {
    current_byte=dos_read_byte(i);
    if(current_byte==0) return i;
  }
}

// =================================================
// Создать новый файл
// =================================================

int file_create(char *filename,char *content) {
  int free_space_begin;
  int req_space;
  int free_space;
  int i;

  // Проверяем корректность имени файла
  if(check_file_name(filename)==FILE_NAME_ERROR) {
    show_decode_result(FILE_NAME_ERROR);
    return FILE_NAME_ERROR;
  }
  
  // Вычисляем есть ли место и откуда начинать запись
  free_space_begin=get_free_space_index();
  free_space=current_disk_size-free_space_begin;
  req_space=strlen(filename)+1+strlen(content)+1;
  if(free_space<req_space) return FILE_TOO_BIG;
  
  // Записываем имя файла
  for(i=0;i!=strlen(filename);i++) {
    dos_write_byte(free_space_begin+i,filename[i]);
  }
  dos_write_byte(free_space_begin+i,0);
  
  // Записываем содержимое
  for(i=0;i!=strlen(content);i++) {
    dos_write_byte(free_space_begin+strlen(filename)+1+i,content[i]);
  }
  dos_write_byte(free_space_begin+strlen(filename)+1+i,0);

  // Возвращаем результат
  return FILE_OK;
}

// =================================================
// Показать приглашение системы
// =================================================

void show_prompt() {
  Serial.println(EMPTY_STRING);
  show_disk_name();
  Serial.print(COMMAND_PROMPT);
}

// =================================================
// Прочитать строку с клавиатуры
// =================================================

char read_str(unsigned char *buff,int max_len) {
  int input_byte;
  int index;
  memset(buff,0,max_len);
  index=0;
  do {
    while(Serial.available()>0) {
      input_byte=Serial.read();
      switch(input_byte) {
        // Ctrl+C
        case 3:
          return 0;
          break;
        // Backspace
        case 8: // Ctrl+H
        case 127: // Backspace
          // Стираем первый байт
          if(index>0) {
            Serial.print((char)input_byte);
            index--;
          }
          buff[index]=0;
          // Поддержка двухбайтовых символов
          // Если второй байт - начало юникодового символа - стираем его тоже
          if(index>0 && buff[index-1]>=0xc0 && buff[index-1]<=0xdf) {
            index--;
            buff[index]=0;
          }
          break;
        // Enter
        case 10:
        case 13:
          Serial.println(EMPTY_STRING);
          break;
        // Прочие символы
        default:
          // Если символ отображаемый или это второй байт юникода
          if(index+1<max_len && (isPrintable(input_byte) || (input_byte>=0x80 && input_byte<=0xbf)))
            {
            buff[index]=input_byte;
            Serial.print((char)input_byte);
            index++;
          // Двухбайтовые символы - первый байт юникода
          } else if(index+2<max_len && (input_byte>=0xc0 && input_byte<=0xdf)) {
            buff[index]=input_byte;
            Serial.print((char)input_byte);
            index++;
          } 
          break;
      }
    }
  } while(input_byte!=13 && input_byte!=10);
  return 1;
}

// =================================================
// Показать содержимое файла
// =================================================

void command_show(char *filename) {
  int content_begin;
  int i;
  char current_byte;

  // Проверяем имя файла
  if(check_file_name(filename)==FILE_NAME_ERROR) {
    show_decode_result(FILE_NAME_ERROR);
    return;
  }
  
  // Ищем начало содержимого файла
  content_begin=find_file_content(filename);
  if(content_begin<0) {
    show_decode_result(content_begin);
    return;
  }

  // Выводим содержимое на экран
  for(i=content_begin;i<current_disk_size;i++) {
    current_byte=dos_read_byte(i);
    if(current_byte==0) break;
    if(current_byte=='\n') Serial.println(EMPTY_STRING);
    else Serial.print(current_byte);
  }
  if(i!=content_begin) Serial.println(EMPTY_STRING);
}

// =================================================
// Дописать строку в файл
// =================================================

void command_append(char *filename) {
  int file_end;
  int free_index;
  unsigned char content[80];
  int content_len;
  int result;
  int i;
  char current_byte;
  int continue_flag;
  
  if(check_file_name(filename)==FILE_NAME_ERROR) {
    show_decode_result(FILE_NAME_ERROR);
    return;
  }

  // Предлагаем ввести строку
  do {
    Serial.print(INPUT_TEXT_PROMPT);
    continue_flag=read_str(content,80);

    if(continue_flag==1 && strlen(content)>0) {
      file_end=find_file_end(filename);
      if(file_end==FILE_NOT_FOUND) {
        result=file_create(filename,content);
        show_decode_result(result);
      } else {
        // Проверяем наличие места
        free_index=get_free_space_index();
        
        // Перевод строки плюс строка
        content_len=strlen(content)+1;
        if(free_index+content_len>current_disk_size) {
          show_decode_result(FILE_TOO_BIG);
          return;
        }
        
        // Сдвигаем файлы вперёд
        for(i=free_index;i>=file_end;i--) {
          current_byte=dos_read_byte(i);
          dos_write_byte(i+content_len,current_byte);
        }
    
        // Записываем новое содержимое
        dos_write_byte(file_end,'\n');
        for(i=0;i!=content_len-1;i++) {
          dos_write_byte(file_end+i+1,content[i]);
        }
        show_decode_result(FILE_OK);
      }
    }
  } while(continue_flag==1);
}

// =================================================
// Перезаписать файл
// =================================================

void command_rewrite(char *filename) {
  if(check_file_name(filename)==FILE_NAME_ERROR) {
    show_decode_result(FILE_NAME_ERROR);
    return;
  }
  command_del(filename);
  command_append(filename);
}

// =================================================
// Удалить файл
// =================================================

void command_del(char *filename) {
  int file_begin;
  int file_end;
  int file_len;
  int i;
  char current_byte;

  if(check_file_name(filename)==FILE_NAME_ERROR) {
    show_decode_result(FILE_NAME_ERROR);
    return;
  }
  
  // Начало файла
  file_begin=find_file_begin(filename);
  if(file_begin==FILE_NOT_FOUND) {
    show_decode_result(file_begin);
    return;
  }
  file_end=find_file_end(filename);
  file_len=file_end-file_begin;
  
  // Сдвигаем файлы назад
  for(i=file_begin;i!=current_disk_size;i++) {
    if((i+file_len+1)<current_disk_size) {
      current_byte=dos_read_byte(i+file_len+1);
    } else {
      current_byte=0;
    }
    dos_write_byte(i,current_byte);
  }

  show_decode_result(FILE_OK);
}

// =================================================
// Вывести версию системы
// =================================================

void command_ver() {
  Serial.println(VERSION_STRING);
}

// =================================================
// Справка
// =================================================

void command_help() {
  Serial.println(HELP_STRING);
}

// =================================================
// Аптайм
// =================================================

void command_uptime(char *params) {
  if(!strcmp(params,"/s")) {
    Serial.print(millis()/1000);
    Serial.println(UNIT_SECONDS);
  } else if(!strcmp(params,"/ms")) {
    Serial.print(millis());
    Serial.println(UNIT_MILLISECONDS);
  } else if(!strcmp(params,"/mcs")) {
    Serial.print(micros());
    Serial.println(UNIT_MICROSECONDS);
  } else {
    long timestamp=millis()/1000;
    // Дни
    if(timestamp>=86400) {
      Serial.print(timestamp/86400);
      Serial.print(UNIT_DAYS);
      Serial.print(" ");
      timestamp%=86400;
    }
    // Часы
    if(timestamp>=3600) {
      Serial.print(timestamp/3600);
      Serial.print(UNIT_HOURS);
      Serial.print(" ");
      timestamp%=3600;
    }
    // Минуты
    if(timestamp>=60) {
      Serial.print(timestamp/60);
      Serial.print(UNIT_MINUTES);
      Serial.print(" ");
      timestamp%=60;
    }
    // Секунды
    Serial.print(timestamp);
    Serial.println(UNIT_SECONDS);
  }
}

// =================================================
// Выполнить введённую команду
// =================================================

void do_command(char *command) {
  int i;
  
  // Убираем пробелы в начале команды
  while(*command==' ') command++;
  
  // Убираем пробелы в конце команды
  for(i=strlen(command)-1;i>=0;i--) {
    if(command[i]==' ') command[i]=0;
    else break;
  }

#ifdef DEBUG
  // Показать содержимое диска
  if(strcmp(command,"dump")==0) {
    command_dump();
    return;
  }
#endif

  // На пустую строку ничего не делаем, ошибкой тоже не является
  if(strcmp(command,"")==0) {
  
  // Показать версию
  } else if(strcmp(command,"ver")==0) {
    command_ver();
  
  // Показать справку
  } else if(strcmp(command,"help")==0) {
    command_help();
  
  // Показать аптайм
  } else if(strncmp(command,"uptime ",7)==0) {
    command_uptime(command+7);
  } else if(strcmp(command,"uptime")==0) {
    command_uptime(command+6);
  
  // Очистить текущий диск
  } else if(strcmp(command,"format")==0) {
    command_format_disk();

  // Показать список файлов
  } else if(strcmp(command,"dir")==0 || strcmp(command,"ls")==0) {
    command_dir();

  // Перейти на диск ram A:
  } else if(strcmp(command,"a:")==0) {
    command_set_disk(RAM_DISK);
  
  // Перейти на диск rom B:
  } else if(strcmp(command,"b:")==0) {
    command_set_disk(ROM_DISK);
  
  // Перейти на диск eeprom C:
  } else if(strcmp(command,"c:")==0) {
    command_set_disk(EEPROM_DISK);
  
  // Показать содержимое файла
  } else if(strncmp(command,"show ",5)==0 || strncmp(command,"type ",5)==0) {
    command_show(command+5);
  
  // Показать содержимое файла
  } else if(strncmp(command,"cat ",4)==0) {
    command_show(command+4);
  
  // Удалить файл
  } else if(strncmp(command,"del ",4)==0) {
    command_del(command+4);
  
  // Удалить файл
  } else if(strncmp(command,"rm ",3)==0) {
    command_del(command+3);
  
  // Дописать файл
  } else if(strncmp(command,"append ",7)==0) {
    command_append(command+7);
  
  // Переписать файл
  } else if(strncmp(command,"rewrite ",8)==0) {
    command_rewrite(command+8);
  
  // Неизвестная команда
  } else {
    Serial.print(UNKNOWN_COMMAND);
  }
}

// =================================================
// Настройка параметров при включении
// =================================================

void setup() {
  // Serial init
  Serial.begin(9600);
  // Format RAM disk
  command_set_disk(RAM_DISK);
  command_format_disk();
  // Show version
  command_ver();
  // Set current drive
  command_set_disk(EEPROM_DISK);
}

// =================================================
// Основной цикл
// =================================================

void loop() {
  unsigned char command_buffer[COMMAND_BUFFER_SIZE];
  
  // Чтение команды
  while(1) {
    show_prompt();
    read_str(command_buffer,COMMAND_BUFFER_SIZE);
    do_command(command_buffer);
  }
}

