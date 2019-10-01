#include <Wire.h>
#include <SD.h>
#include <SPI.h>

#define chipAddress 0x50         // I2C adresse på EEPROM

#define CHIPSELECT 53            // CS Arduino mega
#define maxProgramSize 4096
char fileName[13] = "hej";       //8 karakter filnavn, 3-karakter file extension
char opCode[30];
char operand[30];
char tmp;
byte data[maxProgramSize];
unsigned int cellAddress = 0; // SKAL være delelig med 16
String state = "opCode";
File root;                        //Instans af File
int counter = 0;                  //
unsigned int byteCounter = 2;              //
int byteCounterPrev = 0;
int i2cCount = 0;
int i2c_byteCount = 0;
bool failFlag = false;            // et flag som fortæller om skrivning af eeprom er succesful
int ifFlag[30];                   // Bruger til at fortæller hvor meget står inden i et if-statement.
int ifFlagCounter = 0; 

void setup() {

  Serial.begin(115200);
  Wire.setClock(400000);                         //I2C clockhastighed
  Wire.begin();                                  //I2C intialisering
  Serial.print(F("Initializing SD card..."));

  if (!SD.begin(CHIPSELECT)) {                          //Tjekker for om CS er tilgængelig
    Serial.println(F("Card failed, or not present"));
    while (1);
  }
  Serial.println(F("Card initialized."));


  root = SD.open("/");        //Åbner root på SD-kortet

  printDirectory(root, 0);    //Printer alle filer i root på SDkort til serial

 

  //SLUT PÅ SETUP HER

}

void printDirectory(File dir, int numTabs) { // Scanner igennem alle entries og printer alle filer fra root ud på serial. Eksempel fra SD lib.
  while (1) {
    File entry =  dir.openNextFile();
    if (! entry) {                       // når alle filer er scannet
      break;
    }
    for (byte i = 0; i < numTabs; i++) {   //uint8_t = 1 byte
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {     //kontrollerer, om filen betegnet med dette stinavn er en mappe
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);  //Printer størelsen på de forskellige entries, i Decimal
    }
    entry.close();
  }
}

/*                                                      writeTo
 *  writeTo fungerer ved at benytte EEPROM'ens mulighed,
 *  for at pagewrite. Læs datasheetet for 24LC256(http://www.komponenten.es.aau.dk/fileadmin/komponenten/Data_Sheet/Memory/24LC256.pdf) 
 *  side 7. I stedet for at skrive til hver addresse individuelt, indeholder chippen en intern 64 byte buffer.
 *  Man kan derved skrive mere end 1 byte pr. transmission. Dog har arduinoen kun en 32 byte i2c buffer, hvilket gør
 *  at vi bliver nødt til at dele hver transmission op i blokke af 16 bytes, da det går op i 64 bytes.
 *  Grunden til at det ikke er blokke af 32 bytes, er fordi at man vælger memory addresse med de 2 første bytes 
 *  der sendes i transmissionen derfor tager de 2 bytes af den 32 byte store buffer og har derfor kun 30 tilbage.
 *  Dette går ikke op i 64. Ved at bruge page write skal man kun lave start og stop sekvens for hvert 16. byte.
 *  Det samme gælder for at man ikke skal skrive i2cadresse og memory adresse for hvert byte.
 */
void writeTo(int i2cAddress, unsigned int cellAddress, byte *wData) {
  int blocks;
  int writeCount = 0;
  Serial.print("Bytecount: "); Serial.println(byteCounter); // Debugging
  if ((byteCounter+1) % 16 == 0) blocks = (byteCounter+1) / 16; // Udregning af mængden af transmissioner som skal sendes(blocks)
  else blocks = ((byteCounter+1) / 16) + 1;
  Serial.print("Blocks to be written: "); Serial.println(blocks); // Debuggin
  for (int j = 0; j < blocks; j++) { // Ydre for loop kører hver blok
    Wire.beginTransmission(i2cAddress);// i2c adressen for EEPROM chippen
    Wire.write((byte)((cellAddress + j * 16) >> 8)); // Memory adressens 8 MSB
    Wire.write((byte)((cellAddress + j * 16) & 0xFF)); // Memory adressens 8 LSB
    for (int i = 0; i < 16; i++) // invendigt for loop sender hvert byte i en block.
    {    
      Wire.write(wData[i + j * 16]);
      Serial.print("Writing "); Serial.print((byte)wData[i + j * 16]); Serial.print(" to address: "); Serial.println(cellAddress+(i+j*16)); // debugging af afsendelse     
      if(writeCount == byteCounter) break;
      else writeCount++; 
    }
    Wire.endTransmission();
    delay(6);  // For den interne buffer i eeprom kan nå at følge med
  }
}

byte readFromSpecific(int i2cAddress, unsigned int cellAddress) {
  Wire.beginTransmission(i2cAddress);   //Begynder i2c kommunikation på adresse 80
  Wire.write((byte)(cellAddress >> 8)); //MSB. Efter der er sendt en writeheader så skal der sendes 2 register bytes tilsvarende til de data der skal læses.
  Wire.write((byte)(cellAddress & 0xFF)); //LSB
  Wire.endTransmission();
  Wire.requestFrom(i2cAddress, 1); //Bruges af masteren til at requeste bytes fra slaven. 1 angiver antallet af bytes.

  byte rData = 0;
  if (Wire.available()) {
    rData = Wire.read(); //læser en byte fra slaven
  }
  return rData;
}

byte readFromCurrent(int i2cAddress) {
  Wire.requestFrom(i2cAddress, 1);
  byte rData = 0;
  if (Wire.available()) rData = Wire.read();
  return rData;
  delay(5);      //delay for bufferen kan følge med
}

void loop() {


cellAddress = 0; // SKAL være delelig med 16
state = "opCode";
counter = 0;                  //
byteCounter = 2;              //
byteCounterPrev = 0;
i2cCount = 0;
i2c_byteCount = 0;
failFlag = false;           
ifFlagCounter = 0; 
 Serial.println("Please pick file.");   //Printer til serial efter at have listet alle filer i root

  bool runningState = 1;
  int place = 0;

  while (runningState) {
    while (Serial.available() > 0) {     //Så længe der er data tilgængelig i bufferen
      Serial.println(Serial.available());
      delay(10);                        //delay så serialbufferen kan følge med.
      char temp = (char)Serial.read();   //Gemmer serielt data midlertidigt
      if (temp != 0xA && temp != 0xD) {  //Carriage return og newline (ASCII). Så længe temp ikke er en af dem så skriver den temp til filename
        fileName[place] = temp;
        Serial.print(fileName[place]);
        Serial.print(" written. ");
        Serial.println();
        place++;
      }
      else {                            //Sætter runningstate til 0 og nulterminere filename place
        Serial.print("Here: ");
        Serial.println(fileName[0]);
        fileName[place] = 0x00;
        Serial.println("Null terminated.");
        Serial.println(Serial.available());
        runningState = 0;
        Serial.print("Here: ");
        Serial.println(fileName[0]);
      }
    }
  }

  Serial.print("Opening "); Serial.println(fileName);  // Skriver filen ud til serial
  File dataFile = SD.open(fileName);

  if (dataFile) {                                    //Hvis datafile blev åbnet ok
    while (dataFile.available()) {                   //Parser starter herfra
      Serial.println(state); //Instantieret til opCode state
      tmp = dataFile.read(); //Henter en byte ind
      if (tmp != 0x20 && tmp != 0xD && (state == "opCode" || state == "i2c_state")) { //0x20 Mellerum, 0xD Carriage return  (ASCII)
        opCode[counter] = tmp;
        counter++;
        opCode[counter] = 0x00;
      } else if (tmp != 0xD && (state == "operand" || state == "i2c_state_operand")) { //0xD Carriage return
        operand[counter] = tmp;
        counter++;
        operand[counter] = 0x00;
      } else if (tmp == 0x20) {    //0x20 mellemrum
        if(state == "opCode") state = "operand";
        else if (state == "i2c_state") state = "i2c_state_operand";
        counter = 0;        
      } else if (tmp == 0xD) { //0xD Carriage return og 0x03 end of text. Bliver den brugt???
        if (!strcmp(opCode, "ld")) {      //String compare. Kigger fra første plads i arrayet svare til l, osv..
          data[byteCounter] = 0x00;   //Load ift. instruction list)
          byteCounter++;
        } else if (!strcmp(opCode, "st")) {
          data[byteCounter] = 0x01;   //Store ift. instruction list)
          byteCounter++;
        } else if (!strcmp(opCode, "and")) {
          data[byteCounter] = 0x03;   //And ift. instruction list
          byteCounter++;
        } else if (!strcmp(opCode, "or")) {
          data[byteCounter] = 0x04;  // OR ift. instruction list
          byteCounter++;
        } else if (!strcmp(opCode, "xor")) {
          data[byteCounter] = 0x05;  // XOR ift. instruction list
          byteCounter++;
        } else if (!strcmp(opCode, "i2cDataWr")) {
          data[byteCounter] = 0x06;  
          byteCounter++;
          data[byteCounter] = i2cCount;
          i2cCount++;
          byteCounter++;
        } else if (!strcmp(opCode, "pwm_write")) {
          data[byteCounter] = 0x08;  // pwm_write ift. instruction list
          byteCounter++;
        } else if (!strcmp(opCode, "analog_read")) {
          data[byteCounter] = 0x09;  // analog_write ift. instruction list
          byteCounter++;
        } else if(!strcmp(opCode, "i2cEnd")){
          state = "opCode";
          data[byteCounterPrev] = i2c_byteCount - 1;
          i2c_byteCount = 0;
        } else if (!strcmp(opCode, "ldc")) {
          data[byteCounter] = 0x0A;  // ldc ift. instruction list
          byteCounter++;
        } else if (!strcmp(opCode, ">")) {
          data[byteCounter] = 0x0B;  // > ift. instruction list
          byteCounter++;
        } else if (!strcmp(opCode, "<")) {
          data[byteCounter] = 0x0C;  // < ift. instruction list
          byteCounter++;
        } else if (!strcmp(opCode, "=")) {
          data[byteCounter] = 0x0D;  // = ift. instruction list
          byteCounter++;
        } else if (!strcmp(opCode, "if")) {
          data[byteCounter] = 0x0E;  // if ift. instruction list
          byteCounter = byteCounter + 2;
          ifFlag[ifFlagCounter] = byteCounter;
          ifFlagCounter++;
        } else if (!strcmp(opCode, "endif")) {
          ifFlagCounter--;
          data[ifFlag[ifFlagCounter]-1] = byteCounter-ifFlag[ifFlagCounter]; //argumentet til if             
        } else if (!strcmp(opCode, "get_time")) {
          data[byteCounter] = 0x0F;   //Store ift. instruction list)
          byteCounter++;
        } else if (!strcmp(opCode, "bool")) {
          data[byteCounter] = 0x10;   //Store ift. instruction list)
          byteCounter++;
        } else if (!strcmp(opCode, "not")) {
          data[byteCounter] = 0x80;   //Store ift. instruction list)
          byteCounter++;
        } else if (!strcmp(opCode, "nand")) {
          data[byteCounter] = 0x81;   //Store ift. instruction list)
          byteCounter++;
        }else if (!strcmp(opCode, "nor")) {
          data[byteCounter] = 0x82;   //Store ift. instruction list)
          byteCounter++;
        }else if (!strcmp(opCode, "xnor")) {
          data[byteCounter] = 0x83;   //Store ift. instruction list)
          byteCounter++;
        }
        
        if (state == "operand") {
          data[byteCounter] = atoi(operand); //Konverterer ASCII til integer
          byteCounter++;
          if(!strcmp(opCode, "i2cDataWr")){
            byteCounterPrev = byteCounter;
            Serial.println(byteCounterPrev);
            byteCounter++;
            state = "i2c_state";
          }else state = "opCode";
        }else if(state == "i2c_state_operand"){
          Serial.println("this was run");
          data[byteCounter] = atoi(operand);
          byteCounter++;
          i2c_byteCount++;
          state = "i2c_state";
        }
        
        
        dataFile.read();   //Læser 0xA som er newline i ascii.(spørg mikkel hvis i ikke forstår)    
        counter = 0;
      }
      delay(5);  // Delay for at koden ikke render fra bufferen
    }
    dataFile.close();           //Lukker .txt filen
    data[byteCounter] = 0xFE;
    byteCounter++;
    data[byteCounter] = i2cCount;
    byteCounter++;
    data[byteCounter] = 0xFF;   // End code for parseren
    data[0] = (byte)(byteCounter-2 >> 8);
    data[1] = (byte)(byteCounter-2 & 0xFF);
  }
  else {
    Serial.println("Error opening textfile"); // Hvis dataFile ikke blev åbnet
  }

  for (int i = 0; i <= byteCounter; i++) {
    Serial.println(data[i], DEC);        //Lille kontrol for at tjekke om parseren har virket.
  }
  writeTo(chipAddress, cellAddress, data); // Skriver til EEPROM

  byte eepromTmp = 0;
  
  for (int i = cellAddress; i <= byteCounter+cellAddress; i++) {
    if(i == cellAddress){ //Det skal vælges hvad for en memory adresse der skal læses fra.
      eepromTmp = readFromSpecific(chipAddress, cellAddress); // Specificere samt læser fra den første memory adresse som blev skrevet til
      Serial.print("Address "); Serial.print(i); Serial.print(": "); Serial.println(eepromTmp); //printer fra specifik adresse
      if(eepromTmp != data[i-cellAddress]) failFlag = true; // Hvis det der læses fra EEPROM ikke stemmer overens med det kompilerede, retuneres der med fejl
      //if(failFlag) break;
    }else{
      eepromTmp = readFromCurrent(chipAddress); // benytter EEPROM'ens interne addresse pointer som vokser med 1 hver gang der læses uden en specifik adresse.
                                                // Se datasheetet for 24LC256(http://www.komponenten.es.aau.dk/fileadmin/komponenten/Data_Sheet/Memory/24LC256.pdf) 
                                                // side 9 sektion 8.3 Sequential Read. 
      Serial.print("Address "); Serial.print(i); Serial.print(": "); Serial.println(eepromTmp); //Printer fra aktuelle adresse
      if(eepromTmp != data[i-cellAddress]) failFlag = true; // Hvis det der læses fra EEPROM ikke stemmer overens med det kompilerede, retuneres der med fejl
      //if(failFlag) break;
    }    
  }
  if(!failFlag) Serial.println("Write succesful"); // Hviser brugeren om der har været fejl i programmeringen
  else Serial.println("Error writing to eeprom");
}
