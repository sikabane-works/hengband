/*!
* @file xmlfiles.c
* @brief xml/html�o�͊Ǘ� / Purpose: xml/html output
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
		(void)sprintf(out_val, _("��������t�@�C�� %s �ɏ㏑�����܂���? ", "Replace existing file %s? "), buf);
		if (get_check_strict(out_val, CHECK_NO_HISTORY)) fd = -1;
	}

	if (dump_player_xml_aux(buf))
	{
		prt(_("�L�����N�^���̃t�@�C���ւ̏����o���Ɏ��s���܂����I", "Character dump failed!"), 0, 0);
		(void)inkey();
		return (-1);
	}

	msg_print(_("�L�����N�^���̃t�@�C���ւ̏����o���ɐ������܂����B", "Character dump successful."));
	msg_print(NULL);
	return (0);
}

static errr add_xml_element(xmlTextWriterPtr writer, cptr element_name, cptr val)
{
	int result;
	result = xmlTextWriterStartElement(writer, BAD_CAST element_name);
	if (result < 0) return 1;
	result = xmlTextWriterWriteFormatString(writer, val, 1);
	if (result < 0) return 1;
	result = xmlTextWriterEndElement(writer);
	if (result < 0) return 1;
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

	/*!< �v���C���[�� / Player Name */
	add_xml_element(writer, "name", p_ptr->name);

	result = xmlTextWriterEndElement(writer);
	if (result < 0) return 1;

	result = xmlTextWriterEndDocument(writer);
	if (result < 0) return 1;

	xmlFreeTextWriter(writer);

	return 0;
}
