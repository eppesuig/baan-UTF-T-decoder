/* 
 * File:   pgutft.c
 * Author: Giuseppe Sacco
 *
 * $Id:$
 * Created on 20 aprile 2018, 14.45
 * 
 * Realizza una estensione per postgresql, la quale contiene una funzione
 * chiamata utft_to_utf8() che prende una stringa di bit UTF-T e restituisce
 * una stringa utf8.
 * 
 * In postgresql va dichiarata così (con utente postgresql):
 * CREATE FUNCTION utft_to_utf8(bytea) RETURNS text
 *     AS '/home/giuseppe/src/pgUTF-T/dist/Debug/GNU-Linux/libpgUTF-T.so', 'utft_to_utf8'
 *     LANGUAGE C STRICT;
 * (la directory può essere omessa se il file si trova nella directory indicata
 * dal comando «pg_config --pkglibdir»
 * poi va resa pubblica così (da utente postgresql):
 * GRANT ALL ON FUNCTION utft_to_utf8(bytea) TO ....;
 * 
 * Nota: lo STRICT indica che se l'argomento è NULL, allora il risultato è NULL
 * senza neppure chiamare la funzione.
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include <postgres.h>
#include <fmgr.h>

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif


#define UTFT_BUFSIZE 512

PG_FUNCTION_INFO_V1(utft_to_utf8);

/*
prendo un byte composto da nibble1 nibble0
se il bit più significativo è 0 => codifica a byte singolo, codici unicode tra U+0000 e U+007F
se il bit più signoficativo è 1 => codifica a 4 byte, codici unicode tra U+0080 e U+10FFFF

se byte singolo => la sequenza unicode di 21 bit è 0 0000 0000 0000 nibble1 nibble0
se 4 byte => il primo byte è 0x9b, agli altri sommo 0x0f0000 e poi tolgo il bit più significativo di ciascuno e unisco i restanti 3x7=21 bit

esempio singolo byte: lettera «A», codice U+0041, codifica UTF-T 0100 0001 = 41
esempio 4 byte, simbolo «ç», codice U+00E7, codifica UTF-T 1001 1011 1011 1100 1000 0001 1110 0111 = 9BBC81E7,
 > BC81E7 = 1011 1100 1000 0001 1110 0111
 > tolgo i bit più significativi e applico lo shift: 011 1100 000 0001 110 0111 => 01111 00000000 11100111 = 0F00E7 => 0F0000 + 0000E7
esempio 4 byte, simbolo «€», codice U+20AC, codifica UTF-T 1001 1011 1011 1100 1100 0001 1010 1100 = 9BBCC1AC
 > BCC1AC = 1011 1100 1100 0001 1010 1100
 > tolgo i bit più significativi e applico lo shift: 011 1100 100 0001 010 1100 => 01111 00100000 10101100 = 0F20AC => 0F0000 + 0020AC
esempio 4 byte, simbolo «𝄞», codice U+01D11E, codifica UTF-T 1001 1011 1100 0011 1010 0010 1001 1110 = 9BC3A29E
 > C3A29E = 1100 0011 1010 0010 1001 1110
 > tolgo i bit più significativi e applico lo shift: 100 0011 010 0010 001 1110 => 10000 11010001 00011110 = 10D11E => 0F0000 + 01D11E

analogamente, la cofica utf8 funziona così
tra U+0000 e U+007F => 1 byte con il bit più significativo a 0
tra U+0080 e U+07FF => 2 byte, il primo '110' seguito dai bit 11 10 9 8 7, il secondo '10' seguito dai bit 6 5 4 3 2 1
tra U+0800 e U+FFFF => 3 byte, il primo '1110' seguito dai bit 16 15 14 13, il secondo '10' seguito dai bit 12 11 10 9 8 7, il terzo '10' seguito dai bit 6 5 4 3 2 1
tra U+010000 e U+10FFFF => 4 byte, il primo '11110' seguito dai bit 21 20 19, il secondo '10' seguito dai bit 18 17 16 15 14 13, il terzo '10' seguito dai bit 12 11 10 9 8 7, il quarto '10' seguito dai bit 6 5 4 3 2 1
 */

/**
 * Converte il tipo di dato bytea in UTF-8 assumendo che nel bytea ci sia una stringa codificata in UTF-T.
 * 
 * Nota, in caso di problemi con l'allocazione della memoria, la funzione palloc() termina con una eccezione, senza restituire NULL
 *
 */
Datum utft_to_utf8(PG_FUNCTION_ARGS)
{
    bytea *in = PG_GETARG_BYTEA_P(0);
    text *out = NULL;
    int bufsize = UTFT_BUFSIZE;
    int indexT = 0;
    int index8 = 0;
    const int maxT = VARSIZE(in);
    unsigned char *buffer = palloc(bufsize);

    while (indexT < maxT) {
        unsigned char b = (unsigned char)(in->vl_dat[indexT]);
        if (b == 0x9B) {
            /* Codifica UTF-T multibyte su 4 byte. Il primo deve essere 0x9B, gli altri contengono il dato
             * Se il secondo è 0xBC allora il dato occupa solo gli ultimi 2
             * Se il secondo è 0xBC e il terzo 0x81 allora il dato occupa solo l'ultimo
             * Dei 3, 2 o 1 byte di dati, vanno presi e concatenati solo gli ultimi 7 bit */
            unsigned char b1 = (unsigned char)(in->vl_dat[indexT+1]);
            unsigned char b2 = (unsigned char)(in->vl_dat[indexT+2]);
            unsigned char b3 = (unsigned char)(in->vl_dat[indexT+3]);
            indexT += 4;

            /* Unendo i 21 bit restanti, ho il codice Unicode del carattere */
            unsigned long c = ((b3 & 127) | ((b2 & 127)<<7) | ((b1 & 127)<<14)) - 0x0F0000UL;

            if (c < 0x7F) {
                // codifico c in UTF-8 su un solo byte
                buffer[index8++] = (unsigned char)c;
            }
            else if (c < 0x07FF) {
                // codifico c in UTF-8 in 2 byte
                buffer[index8++] = 0xC0UL | ((c & 0x7C0UL)>>6);
                buffer[index8++] = (0x02UL << 6) | ((c & 0x3FUL));
            }
            else if (c < 0xFFFF) {
                // codifico c in UTF-8 in 3 byte
                buffer[index8++] = 0xE0UL | ((c & 0xF000UL)>>12);
                buffer[index8++] = (0x02UL << 6) | ((c & 0xFC0UL)>>6);
                buffer[index8++] = (0x02UL << 6) | ((c & 0x3FUL));
            }
            else {
                // codifico c in UTF-8 in 4 byte
                buffer[index8++] = 0xF0UL | ((c & 0xF000UL)>>18);
                buffer[index8++] = (0x02UL << 6) | ((c & 0x3F00UL)>>12);
                buffer[index8++] = (0x02UL << 6) | ((c & 0xFC0UL)>>6);
                buffer[index8++] = (0x02UL << 6) | ((c & 0x3FUL));
            }
        }
        else if (b & (1<<7)) {
            unsigned long c = b;

            indexT += 2;

            // codifico c in UTF-8 in 2 byte
            buffer[index8++] = 0xC0UL | ((c & 0x7C0UL)>>6);
            buffer[index8++] = (0x02UL << 6) | ((c & 0x3FUL));
        }
        else {
            buffer[index8++] = b;
            indexT += 1;
        }

        if (index8 > (bufsize-4)) {
            bufsize += UTFT_BUFSIZE;
            buffer = repalloc(buffer, bufsize);
        }
    }

    out = palloc(VARHDRSZ + index8);
    SET_VARSIZE(out, index8);
    memcpy(&out->vl_dat[0], &buffer[0], index8);

    pfree(buffer);

    PG_RETURN_TEXT_P(out);
}
