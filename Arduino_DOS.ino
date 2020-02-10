#include <EEPROM.h>
#include <avr/pgmspace.h>
#include <Wire.h>
#include <ctype.h>

// Размеры буферов
#define COMMAND_BUFFER_SIZE 40
#define FILENAME_BUFFER_SIZE 40
#define STRING_BUFFER_SIZE 50

// Коды ошибок
#define FILE_NOT_FOUND -1
#define FILE_TOO_BIG -2
#define FILE_NAME_ERROR -3
#define FILE_OK 1

// Строки
#define COMMAND_PROMPT F(":>")
#define INPUT_TEXT_PROMPT F("Text (Ctrl+C to end): ")
#define VERSION_STRING F("Arduino DOS ver 1.8")
#define HELP_STRING F("Commands: ver help uptime format dir show del append rewrite")
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
#define EMPTY_STRING F("")

// ROM диск
const char PROGMEM rom_disk[]="\
changelog\v\
10.2.2017 16:31 v1.1 added uptime command\n\
10.2.2017 17:31 v1.3 more space on RAM disk\n\
10.2.2017 18:50 v1.4 added ROM disk\n\
10.2.2017 18:50 v1.5 added more ROM disk files\n\
17.2.2017 11:39 v1.6 added utf-8 support to files\n\
17.2.2017 12:11 v1.7 added multi-line input\n\
10.2.2020 15:00 v1.8 added dynamic disk names and I2C EEPROM support
\v\
about\v\
Author - Tsarev Vladimir\v\
help\v\
Available commands:\n\
\n\
ver     show current version info\n\
help    show short help\n\
uptime  show uptime\n\
\n\
X: change current disk to X:\n\
format  cleanup current disk\n\
dir     show current disk files\n\
\n\
show    show file content\n\
del     delete file\n\
append  add string to end of file (also creates new if not exists)\n\
rewrite erase file and add string\v\
";

// Режим отладки - когда включен, есть команда dump
#define DEBUG

// Диски

// Общее число дисков в системе
#define DISK_COUNT 6

// Типы дисков
#define DISK_TYPE_NONE 0
#define DISK_TYPE_RAM 1
#define DISK_TYPE_ROM 2
#define DISK_TYPE_EEPROM 3
#define DISK_TYPE_I2C 4
#define DISK_TYPE_SD 5

// Размеры дисков
#define DISK_SIZE_EEPROM (EEPROM.length())
//#define EEPROM_DISK_SIZE 1024
#define DISK_SIZE_RAM 512
#define DISK_SIZE_ROM strlen(rom_disk)

// Описание типа диска
#define DISK_DESC_EEPROM F("EEPROM")
#define DISK_DESC_RAM F("RAM")
#define DISK_DESC_ROM F("ROM")
#define DISK_DESC_I2C F("I2C")
#define DISK_DESC_SD F("SD")

// Диск после загрузки
#define DISK_SYMBOL_DEFAULT 'C'

// RAM диск
char ram_disk[DISK_SIZE_RAM];

// Маппинг дисков, утанавливается командой disk
// disk {a|b|c|d} {ram|rom|eeprom|i2c|sd} [i2c_address] [size]
struct disk_mapping {
  char symbol; // Буква диска
  char type; // Тип диска
  char address; // Адрес в I2C/SPI pin
  long size; // Размер диска
} disk_mapping[DISK_COUNT];

// Переменные
char current_disk_index;

// =================================================
// Прочитать байт с текущего диска
// =================================================

int dos_read_byte(int address) {
  int b;
  if(address < 0 || address >= disk_mapping[current_disk_index].size) return -1;
  switch(disk_mapping[current_disk_index].type) {
    case DISK_TYPE_NONE: return -2; break;
    case DISK_TYPE_EEPROM: return EEPROM.read(address); break;
    case DISK_TYPE_RAM: return ram_disk[address]; break;
    case DISK_TYPE_ROM:
      b = pgm_read_byte_near(rom_disk + address);
      if(b == '\v') b = 0;
      return b;
      break;
    case DISK_TYPE_I2C:
      Wire.beginTransmission(disk_mapping[current_disk_index].address);
      Wire.write(address >> 8); // Старший байт
      Wire.write(address & 0xFF); // Младший байт
      Wire.endTransmission();

      Wire.beginTransmission(disk_mapping[current_disk_index].address);
      Wire.requestFrom(disk_mapping[current_disk_index].address, 1);
      b = Wire.read();
      return b;
      break;
  }
}

// =================================================
// Записать байт на текущий диск
// =================================================

int dos_write_byte(int address, char data) {
  if(address < 0 || address >= disk_mapping[current_disk_index].size) return -1;
  switch(disk_mapping[current_disk_index].type) {
    case DISK_TYPE_NONE: return -2; break;
    case DISK_TYPE_EEPROM: EEPROM.write(address, data); break;
    case DISK_TYPE_RAM: ram_disk[address] = data; break;
    case DISK_TYPE_ROM: return -3; break;
    case DISK_TYPE_I2C:
      Wire.beginTransmission(disk_mapping[current_disk_index].address);
      Wire.write(address >> 8); // Старший байт
      Wire.write(address & 0xFF); // Младший байт
      Wire.write(data); // Данные
      delay(5);
      Wire.endTransmission();
    break;
  }
}

// =================================================
// Выбрать текущий диск
// =================================================

void command_set_disk(char disk_symbol) {
  int disk_index;
  if(!isalpha(disk_symbol)) return;
  disk_symbol = toupper(disk_symbol);
  
  for(disk_index = 0; disk_index != DISK_COUNT; disk_index ++) {
    if(disk_symbol == disk_mapping[disk_index].symbol) {
      current_disk_index = disk_index;
      break;
    }
  }
}

// =================================================
// Очистить текущий диск
// =================================================

void command_format_disk() {
  int i;
  int percentage, prev_percentage;
  
  Serial.print(F("Formatting disk "));
  Serial.print(disk_mapping[current_disk_index].symbol);
  Serial.println(F(":"));
  
  for(i = 0; i != disk_mapping[current_disk_index].size; i++) {
    percentage = i * 100 / disk_mapping[current_disk_index].size;
    if(percentage != prev_percentage) {
      Serial.print(percentage);
      Serial.print(F(" %\r"));
    }
    prev_percentage = percentage;
    dos_write_byte(i, 0);
  }
  Serial.println(F("Completed"));
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
  for(i = 0; i != disk_mapping[current_disk_index].size; i++) {
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
  for(i = 0; i != disk_mapping[current_disk_index].size; i++) {
    if((i%16)==0) {
      if(i>0) {
        Serial.println(EMPTY_STRING);
      }
      sprintf(buff,"0x%04X | ",i);
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
// Показать список дисков
// =================================================
void command_disks() {
  int disk_index;
  for(disk_index = 0; disk_index != DISK_COUNT; disk_index ++) {
    show_disk_info(disk_index);
  }
}

// =================================================
// Показать букву диска
// =================================================

void show_disk_name(int disk_index) {
  Serial.print(disk_mapping[disk_index].symbol);
}

// =================================================
// Показать информацию о диске
// =================================================

void show_disk_info(int disk_index) {
  show_disk_name(disk_index);
  if(disk_mapping[disk_index].type == DISK_TYPE_NONE) return;
  
  Serial.print(F(": is a "));
  switch(disk_mapping[disk_index].type) {
    case DISK_TYPE_RAM: Serial.print(DISK_DESC_RAM); break;
    case DISK_TYPE_ROM: Serial.print(DISK_DESC_ROM); break;
    case DISK_TYPE_EEPROM: Serial.print(DISK_DESC_EEPROM); break;
    case DISK_TYPE_I2C: Serial.print(DISK_DESC_I2C); break;
    case DISK_TYPE_SD: Serial.print(DISK_DESC_SD); break;
  }
  Serial.println(F(" disk"));
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
  show_disk_info(current_disk_index);

  // Читаем весь диск в поисках файлов
  Serial.println(FILE_LIST_PREFIX);
  name_flag=1;
  name_present=0;
  for(i = 0; i != disk_mapping[current_disk_index].size; i++) {
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
  Serial.print(disk_mapping[current_disk_index].size);
  Serial.println(UNIT_BYTES);
  
  // Получаем индекс первого свободного байта
  // Так как байты считаются с 0, то это одновременно количество занятых байт на диске
  // Выводим информацию о месте на диске
  used_space=get_free_space_index();
  Serial.print(USED_SPACE);
  Serial.print(used_space);
  Serial.println(UNIT_BYTES);
  Serial.print(FREE_SPACE);
  Serial.print(disk_mapping[current_disk_index].size - used_space);
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
int check_file_name(unsigned char *filename) {
  int i;
  // Если длина имени 0
  if(strlen((const char*)filename)==0) return FILE_NAME_ERROR;
  // Если есть пробелы или служебные символы
  for(i=0;i!=strlen((const char*)filename);i++) {
    if(!isPrintable(filename[i]) || isspace(filename[i])) return FILE_NAME_ERROR;
  }
  return FILE_OK;
}

// =================================================
// Найти начало имени файла
// =================================================

int find_file_begin(unsigned char *filename) {
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
  
  for(i = 0 ; i != disk_mapping[current_disk_index].size; i++) {
    current_byte=dos_read_byte(i);
    if(current_byte==0) {
      if(name_flag==1) {
        // Если это было имя, и оно закончилось - сверяем
        if(strcmp((const char*)filename,name_buffer)==0) {
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

int find_file_content(unsigned char *filename) {
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
  
  for(i = file_begin; i != disk_mapping[current_disk_index].size; i++) {
    current_byte=dos_read_byte(i);
    if(current_byte==0) return (i+1);
  }
}

// =================================================
// Найти первый байт после конца файла
// =================================================

int find_file_end(unsigned char *filename) {
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
  for(i = file_content; i != disk_mapping[current_disk_index].size; i++) {
    current_byte=dos_read_byte(i);
    if(current_byte==0) return i;
  }
}

// =================================================
// Создать новый файл
// =================================================

int file_create(unsigned char *filename,unsigned char *content) {
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
  free_space_begin = get_free_space_index();
  free_space = disk_mapping[current_disk_index].size - free_space_begin;
  req_space = strlen((const char*)filename) + 1 + strlen((const char*)content) + 1;
  if(free_space<req_space) return FILE_TOO_BIG;
  
  // Записываем имя файла
  for(i=0;i!=strlen((const char*)filename);i++) {
    dos_write_byte(free_space_begin+i,filename[i]);
  }
  dos_write_byte(free_space_begin+i,0);
  
  // Записываем содержимое
  for(i=0;i!=strlen((const char*)content);i++) {
    dos_write_byte(free_space_begin+strlen((const char*)filename)+1+i,content[i]);
  }
  dos_write_byte(free_space_begin+strlen((const char*)filename)+1+i,0);

  // Возвращаем результат
  return FILE_OK;
}

// =================================================
// Показать приглашение системы
// =================================================

void show_prompt() {
  Serial.println(EMPTY_STRING);
  show_disk_name(current_disk_index);
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

void command_show(unsigned char *filename) {
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
  for(i = content_begin; i < disk_mapping[current_disk_index].size; i++) {
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

void command_append(unsigned char *filename) {
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

    if(continue_flag==1 && strlen((const char*)content)>0) {
      file_end=find_file_end(filename);
      if(file_end==FILE_NOT_FOUND) {
        result=file_create(filename,content);
        show_decode_result(result);
      } else {
        // Проверяем наличие места
        free_index=get_free_space_index();
        
        // Перевод строки плюс строка
        content_len=strlen((const char*)content)+1;
        if(free_index+content_len > disk_mapping[current_disk_index].size) {
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

void command_rewrite(unsigned char *filename) {
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

void command_del(unsigned char *filename) {
  int file_begin;
  int file_end;
  int file_len;
  int i;
  char current_byte;

  if(check_file_name(filename) == FILE_NAME_ERROR) {
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
  for(i = file_begin; i != disk_mapping[current_disk_index].size; i++) {
    if((i + file_len + 1) < disk_mapping[current_disk_index].size) {
      current_byte = dos_read_byte(i + file_len + 1);
    } else {
      current_byte = 0;
    }
    dos_write_byte(i, current_byte);
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

void command_uptime(unsigned char *params) {
  if(!strcmp((const char*)params,"/s")) {
    Serial.print(millis()/1000);
    Serial.println(UNIT_SECONDS);
  } else if(!strcmp((const char*)params,"/ms")) {
    Serial.print(millis());
    Serial.println(UNIT_MILLISECONDS);
  } else if(!strcmp((const char*)params,"/mcs")) {
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

void do_command(unsigned char *command) {
  int i;
  
  // Убираем пробелы в начале команды
  while(*command==' ') command++;
  
  // Убираем пробелы в конце команды
  for(i=strlen((const char*)command)-1;i>=0;i--) {
    if(command[i]==' ') command[i]=0;
    else break;
  }

#ifdef DEBUG
  // Показать содержимое диска
  if(strcmp((const char*)command,"dump")==0) {
    command_dump();
    return;
  }
#endif

  // На пустую строку ничего не делаем, ошибкой тоже не является
  if(strcmp((const char*)command,"")==0) {
  
  // Показать версию
  } else if(strcmp((const char*)command,"ver")==0) {
    command_ver();
  
  // Показать справку
  } else if(strcmp((const char*)command,"help")==0) {
    command_help();
  
  // Показать аптайм
  } else if(strncmp((const char*)command,"uptime ",7)==0) {
    command_uptime(command+7);
  } else if(strcmp((const char*)command,"uptime")==0) {
    command_uptime(command+6);
  
  // Очистить текущий диск
  } else if(strcmp((const char*)command,"format")==0) {
    command_format_disk();

  // Показать список файлов
  } else if(strcmp((const char*)command,"dir")==0 || strcmp((const char*)command,"ls")==0) {
    command_dir();

  // Показать список дисков
  } else if(strcmp((const char*)command,"disks")==0) {
    command_disks();

  // Перейти на указанный диск
  } else if(command[1] == ':' && command[2] == 0) {
    command_set_disk(command[0]);

  // Показать содержимое файла
  } else if(strncmp((const char*)command,"show ",5)==0 || strncmp((const char*)command,"type ",5)==0) {
    command_show(command+5);
  
  // Показать содержимое файла
  } else if(strncmp((const char*)command,"cat ",4)==0) {
    command_show(command+4);
  
  // Удалить файл
  } else if(strncmp((const char*)command,"del ",4)==0) {
    command_del(command+4);
  
  // Удалить файл
  } else if(strncmp((const char*)command,"rm ",3)==0) {
    command_del(command+3);
  
  // Дописать файл
  } else if(strncmp((const char*)command,"append ",7)==0) {
    command_append(command+7);
  
  // Переписать файл
  } else if(strncmp((const char*)command,"rewrite ",8)==0) {
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
  while (!Serial);
  
  Serial.println(F("Loading Arduino DOS..."));

  // EEPROM info
  Serial.print("Built-in EEPROM size: ");
  Serial.print((int)DISK_SIZE_EEPROM);
  Serial.println(" bytes");

  // Init disk mapping
  Serial.println(F("Initial disk mapping..."));
  int disk_index;
  for(disk_index = 0; disk_index != DISK_COUNT; disk_index++) {
    disk_mapping[disk_index].symbol = 0;
    disk_mapping[disk_index].type = 0;
    disk_mapping[disk_index].address = 0;
    disk_mapping[disk_index].size = 0;
  }  

  // RAM disk init
  Serial.println(F("Mounting RAM disk..."));
  disk_mapping[0].type = DISK_TYPE_RAM;
  disk_mapping[0].symbol = 'A';
  disk_mapping[0].address = 0;
  disk_mapping[0].size = DISK_SIZE_RAM;
  // Format RAM disk
  command_set_disk('A');
  command_format_disk();

  // ROM disk init
  Serial.println(F("Mounting ROM disk..."));
  disk_mapping[1].type = DISK_TYPE_ROM;
  disk_mapping[1].symbol = 'B';
  disk_mapping[1].address = 0;
  disk_mapping[1].size = DISK_SIZE_ROM;
  
  // EEPROM disk init
  Serial.println(F("Mounting EEPROM disk..."));
  disk_mapping[2].type = DISK_TYPE_EEPROM;
  disk_mapping[2].symbol = 'C';
  disk_mapping[2].address = 0;
  disk_mapping[2].size = DISK_SIZE_EEPROM;
  
  // Scanning I2C devices
  Serial.println(F("Checking I2C devices..."));
  byte error,address;
  Wire.begin();
  for(address = 1; address < 127; address++ ) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print(F("I2C device found at address 0x"));
      if (address<16) Serial.print("0");
      Serial.print(address,HEX);
      Serial.println("  !");
    } else if (error==4) {
      Serial.print(F("Unknown error at address 0x"));
      if (address<16) Serial.print("0");
      Serial.println(address,HEX);
    }    
  }
  Serial.println(F("I2C scanning complete"));
  
  // I2C EEPROM disk init
  Serial.println(F("Mounting I2C disk..."));
  disk_mapping[3].type = DISK_TYPE_I2C;
  disk_mapping[3].symbol = 'D';
  disk_mapping[3].address = 0x50;
  disk_mapping[3].size = 32768;

  // Show version
  command_ver();
  // Set current drive
  command_set_disk(DISK_SYMBOL_DEFAULT);

  Serial.println(F("Rinning autoexec script..."));
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
