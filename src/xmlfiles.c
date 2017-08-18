/*!
* @file xmlfiles.c
* @brief xml/html出力管理 / Purpose: xml/html output
* @date 2014/01/28
* @author
* <pre>
* 2017 Deskull\n
* </pre>
*/

#include "angband.h"

#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>

static errr dump_player_xml_aux(cptr name);

errr dump_player_xml(cptr name)
{
	int fd = -1;
	FILE *fff = NULL;
	char buf[1024];

	path_build(buf, sizeof(buf), ANGBAND_DIR_USER, name);

	FILE_TYPE(FILE_TYPE_TEXT);
	fd = fd_open(buf, O_RDONLY);

	/* Existing file */
	if (fd >= 0)
	{
		char out_val[160];
		(void)fd_close(fd);
		(void)sprintf(out_val, _("現存するファイル %s に上書きしますか? ", "Replace existing file %s? "), buf);
		if (get_check_strict(out_val, CHECK_NO_HISTORY)) fd = -1;
	}

	if (dump_player_xml_aux(buf))
	{
		prt(_("キャラクタ情報のファイルへの書き出しに失敗しました！", "Character dump failed!"), 0, 0);
		(void)inkey();
		return (-1);
	}

	msg_print(_("キャラクタ情報のファイルへの書き出しに成功しました。", "Character dump successful."));
	msg_print(NULL);
	return (0);
}

static errr dump_player_xml_aux(cptr name)
{
	xmlTextWriterPtr writer;
	int result;

	writer = xmlNewTextWriterFilename(name, 0);
	if(writer == NULL) return 1;

	result = xmlTextWriterStartDocument(writer, NULL, "UTF-8", NULL);
	if (result < 0) return 1;

	result = xmlTextWriterStartElement(writer, BAD_CAST "hengband");
	if (result < 0) return 1;

	result = xmlTextWriterEndElement(writer);
	if (result < 0) return 1;

	result = xmlTextWriterEndDocument(writer);
	if (result < 0) return 1;

	xmlFreeTextWriter(writer);

	return 0;
}
