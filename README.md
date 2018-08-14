# baan-UTF-T-decoder
Decoding UTF-T texts in PostgreSQL

This small extension program permits to decode the text tables of Infor baan IV or ERP LN.
Those texts are encoded in UTF-T, so they need to be converted into UTF-8 for being used in
a database.

While PostgreSQL is not currently supported by ERP LN, we connect it to baan or LN database
using foreign data wrapper. This way we may develop applications on PostgreSQL.

# INSTALL
In order to compile this extension, verify that you have `pg_config` in your path (it
should be part of the postgresql development tools).
Then, execute:

`gcc -I$(pg_config --includedir) -fPIC -MMD -MP -MF pgutft.o.d -o pgutft.o pgutft.c`
`gcc -o libpgUTF-T.so pgutft.o -shared -fPIC`
`cp pgutft.o $(pg_config --pkglibdir)`

once the library is installed in the postgresql extension directory, connect to your database
as super user and execute

`CREATE FUNCTION utft_to_utf8(bytea) RETURNS text
      AS 'libpgUTF-T.so', 'utft_to_utf8'
      LANGUAGE C STRICT;`

If you want to let other users uses it, grant them the access to the function this way:
`GRANT ALL ON FUNCTION utft_to_utf8(bytea) TO `username`;`

# USAGE

if your text has many lines, join them as bitea, then pass it to `utft_to_utf8()` function:

`select utft_to_utf8(text_from_baan_table);`
