#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <getopt.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <glib.h>
#include <gmime/gmime.h>
#include <ctype.h>

void output_content(FILE *output, char *content) {
}

char *convert_to_utf8(const char *string, const char *charset) {
  const char *utf8, *local;
  iconv_t local_to_utf8;
  char *result;

  //g_mime_charset_init();
	
  utf8 = g_mime_charset_name("utf-8");
  local = g_mime_charset_name(charset);
  local_to_utf8 = iconv_open(utf8, local);

  result = g_mime_iconv_strdup(local_to_utf8, string);

  return result;
}

void transform_simple_part(FILE *output, GMimePart* part) {
  GMimeContentType* ct = 0;
  gchar* content = 0;
  unsigned long contentLen = 0;
  char content_type[128];
  char *mcontent, *p, *ccontent = NULL;
  const char *charset = NULL;
  GMimeDataWrapper *wrapper;
  GMimeStream *stream;
  char *use_content;
  
  ct = g_mime_object_get_content_type(GMIME_OBJECT(part));

  if (ct == NULL ||
      ct->type == NULL ||
      ct->subtype == NULL) {
    strcpy(content_type, "text/plain");
  } else {
    charset = g_mime_content_type_get_parameter(ct, "charset");
    snprintf(content_type, sizeof(content_type), "%s/%s", 
	     ct->type, ct->subtype);
  }

  if (charset == NULL)
    charset = "utf-8";

  for (p = content_type; *p; p++) 
    *p = tolower(*p);

  /* We copy over the content and zero-terminate it. */
  mcontent = malloc(contentLen + 1);
  wrapper = g_mime_part_get_content_object(part);
  stream = g_mime_data_wrapper_get_stream(wrapper);
  g_mime_stream_read(stream, content, contentLen);
  memcpy(mcontent, content, contentLen);
  *(mcontent + contentLen) = 0;
  use_content = mcontent;

  /* Convert contents to utf-8.  If the conversion wasn't successful,
     we use the original contents. */
  if (strcmp(charset, "utf-8")) {
    ccontent = convert_to_utf8(mcontent, charset);
    if (ccontent)
      use_content = ccontent;
  }

  output_content(output, use_content);
  
  free(mcontent);
  if (ccontent != NULL)
    free(ccontent);
}

void transform_part(FILE *output, GMimeObject *mime_part);

void transform_multipart(FILE *output, GMimeMultipart *mime_part) {
  const GMimeContentType* ct = NULL;
  GMimeObject *child;
  GMimeObject *preferred = NULL;
  char *type, *subtype = NULL;
  int number_of_children = g_mime_multipart_get_count(mime_part);
  int nchild = 0;

  ct = g_mime_object_get_content_type(GMIME_OBJECT(mime_part));

  if (ct != NULL) 
    subtype = ct->subtype;

  if (subtype == NULL)
    subtype = "mixed";

  if (! strcmp(subtype, "alternative")) {
    /* This is multipart/alternative, so we need to decide which
       part to output. */
    while (nchild < number_of_children) {
      child = g_mime_multipart_get_part(mime_part, nchild++);
      ct = g_mime_object_get_content_type(GMIME_OBJECT(child));
      if (ct == NULL) {
	type = "text";
	subtype = "plain";
      } else {
	type = ct->type? ct->type: "text";
	subtype = ct->subtype? ct->subtype: "plain";
      }
	  
      if (! strcmp(type, "multipart") ||
	  ! strcmp(type, "message")) 
	preferred = child;
      else if (! strcmp(type, "text")) {
	if (! strcmp(subtype, "html"))
	  preferred = child;
	else if (! strcmp(subtype, "plain") && preferred == NULL)
	  preferred = child;
      }
    }

    if (! preferred)
      /* Use the last child as the preferred. */
      child = g_mime_multipart_get_part(mime_part, number_of_children);

    transform_part(output, preferred);

  } else {
    /* Multipart mixed and related. */
    while (nchild < number_of_children) {
      child = g_mime_multipart_get_part(mime_part, nchild++);
      transform_part(output, child);
    }
  }
}

void transform_message(FILE *output, GMimeMessage *msg);

void transform_part(FILE *output, GMimeObject *mime_part) {
  if (GMIME_IS_MESSAGE_PART(mime_part)) {
    GMimeMessagePart *msgpart = GMIME_MESSAGE_PART(mime_part);
    GMimeMessage *msg = g_mime_message_part_get_message(msgpart);
    transform_message(output, msg);
    g_object_unref(msg);
  } else if (GMIME_IS_MULTIPART(mime_part)) {
    transform_multipart(output, GMIME_MULTIPART(mime_part)); 
  } else {
    transform_simple_part(output, GMIME_PART(mime_part));
  }
}

char *clean_from(char *from) {
  InternetAddress *iaddr;
  InternetAddressList *iaddr_list;

  if ((iaddr_list = internet_address_list_parse_string(from)) != NULL) {
    iaddr = internet_address_list_get_address(iaddr_list, 0);
    if (iaddr->name != NULL) {
      strcpy(from, iaddr->name);
      
      /* There's a bug in gmimelib that may leave a closing paren in
	 the name field. */
      if (strrchr(from, ')') == from + strlen(from) - 1) 
	*strrchr(from, ')') = 0;
    }
    g_object_unref(iaddr_list);
  }
  return from;
}

void transform_message(FILE *output, GMimeMessage *msg) {
  transform_part(output,  msg->mime_part); 
}

void read_file(FILE *output, int input) {
  GMimeStream *stream;
  GMimeMessage *msg;
  time_t time;
  int tz;
  char *from;
  
  stream = g_mime_stream_fs_new(input);
  msg = g_mime_parser_construct_message(g_mime_parser_new_with_stream(stream));

  g_mime_message_get_date(msg, &time, &tz);
  from = clean_from((char*)g_mime_object_get_header((GMimeObject*)msg, "From"));

  fprintf(output, "<span class=from>%s</span>", from);
  
  transform_part(output,  msg->mime_part); 

  g_object_unref(stream);
}

void read_files(FILE *output, char **inputs) {
  while (*inputs) {
    int input = open(*inputs, O_RDONLY);
    if (input < 0)
      fprintf(stderr, "No such file: %s\n", *inputs);
    else
      read_file(output, input);
    inputs++;
  }
}


int main(int argc, char **argv) {
  char *output_name, *tmp_name;
  FILE *output;
  
  g_type_init();
  g_mime_init(0);
  
  if (argc < 3) {
    printf("Usage: woof <output-file> <input-files...>\n");
    exit(-1);
  }

  output_name = argv[1];
  tmp_name = malloc(strlen(output_name) + 4);
  strcpy(tmp_name, output_name);
  strcat(tmp_name, ".tmp");

  output = fopen(tmp_name, "w");
  if (! output) {
    fprintf(stderr, "While opening %s:\n", tmp_name);
    perror("Opening output file");
    exit(-1);
  }

  read_files(output, argv + 2);

  fclose(output);
  rename(tmp_name, output_name);
  
  return 0;
}
