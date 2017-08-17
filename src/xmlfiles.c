/*!
* @file xmlfiles.c
* @brief xml/htmlèoóÕä«óù / Purpose: xml/html output
* @date 2014/01/28
* @author
* <pre>
* 2017 Deskull\n
* </pre>
*/

#include "angband.h"

#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>

void dumpPlayerXml(void)
{
	xmlTextWriterPtr  writer;

	writer = xmlNewTextWriterFilename("test.xml", 0);
	xmlFreeTextWriter(writer);
}
