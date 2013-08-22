#include "websockframe.h"
#include <string.h>
#include <arpa/inet.h> /* for uint16_t, ntohs, etc */

int
unmask_l(unsigned char *input, size_t input_len, unsigned char *output,
	size_t *output_len)
{
	size_t index;
	unsigned char *mask = input;
	size_t len = 0;
	if (input_len < 4) /* input buffer too small */
		return 0;
	if (*output_len + 4 < input_len) /* output buffer too small */
		return 0;
	index = 0;
	input += 4;
	input_len -= 4;
	len = 0;
	for (; input_len > 0; input++, output++, input_len--, len++)
	{
		*output = (*input ^ mask[index]);
		index++;
		if (index == 4)
			index = 0;
	}
	*output_len = len;
	
	return 1;
}

wsfbuffer_t *
wsfb_create(jsconf_t *conf)
{
	wsfbuffer_t *buffer = (wsfbuffer_t*) malloc(sizeof(*buffer));
	if (buffer == NULL)
		goto Error;
	buffer->buffer = buffer_create(conf->max_message_size);
	if (buffer->buffer == NULL)
		goto Error;
	return buffer;

Error:
	free(buffer);
	return NULL;
}

void
wsfb_delete(wsfbuffer_t *buffer)
{
	buffer_delete(buffer->buffer);
	free(buffer);
}

int
wsfb_append(wsfbuffer_t *buffer, unsigned char *data, size_t len)
{
	int res;
	
	res = buffer_append(buffer->buffer, data, len);
	return res;
}

int
wsfb_have_message(wsfbuffer_t *buffer)
{
	return (wsfb_get_length(buffer) != -1);
}

long
wsfb_get_length(wsfbuffer_t *buffer)
{
	int res;
	int opcode;
	size_t length;
	
	res = wsfb_get_message(buffer, &opcode, NULL, &length);
	if (!res)
		return -1;
	return length;
}

int
wsfb_get_message(wsfbuffer_t *buffer, int *opcode, unsigned char **data,
	size_t *output_length)
{
	size_t length = buffer_get_length(buffer->buffer);
	size_t message_length;
	unsigned char *frame;
	size_t header_length;
	unsigned char *message;
	int res = 0;
	unsigned char *tmp_data;
	int opcode_tmp;

	if (length < 2)
		return 0;
	
	tmp_data = NULL;
	buffer_peek_data(buffer->buffer, &frame, &length);
	message_length = frame[1] & 0x7f;
	opcode_tmp = frame[0] & 0x0f;
	if (message_length == 126)
	{
		/* RFC-6455: payload length is encoded in next two bytes (2 and 3) */
		uint16_t netshort;
		if (length < 4)
			goto Exit;
		netshort = *(uint16_t*) (frame + 2);
		message_length = ntohs(netshort);
		header_length = 8;
	}
	else if (message_length == 127)
	{
		/* RFC-6455: payload length is encoded in next 8 bytes (2 to 9) */
		/* In the current implementation, we don't deal with message lengths
		   that don't fit in 4 bytes */
		uint32_t netlong;
		if (length < 10)
			goto Exit;
		netlong = *(uint32_t*) (frame + 6);
		message_length = ntohl(netlong);
		header_length = 14;
	}
	else
		header_length = 6;
	
	if (header_length + message_length > length)
		goto Exit;
	message = frame + header_length - 4; /* message points at the mask */
	*output_length = message_length;
	if (data != NULL)
	{
		tmp_data = (unsigned char*) malloc(message_length);
		if (tmp_data == NULL)
			goto Exit;
		res = unmask_l(message, message_length + 4, tmp_data, output_length);
		buffer_remove_data(buffer->buffer, length);
		goto Exit;
	}
	res = 1;

Exit:
	if (data != NULL)
	{
		if (res)
			*data = tmp_data;
		else
			free(tmp_data);
	}
	if (res)
		*opcode = opcode_tmp;
	return res;
}

