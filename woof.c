#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <gmime/gmime.h>
#include <sys/stat.h>
#include <ctype.h>

int make_directory (const char *dir) {
  struct stat stat_buf;

  if (stat(dir, &stat_buf) == -1) {
    if (mkdir(dir, 0777) == -1) {
      fprintf(stderr, "Can't create directory %s\n", dir);
      perror("weft");
      return 0;
    } 
    return 1;
  } else if (S_ISDIR(stat_buf.st_mode)) {
    return 1;
  }
  return 0;
}

void ensure_directory(const char *file_name) {
  char *dir = malloc(strlen(file_name) + 1);
  char *s = dir + 1;
  
  strcpy(dir, file_name);
  
  while (*s != 0) {
    while (*s != 0 && *s != '/')
      s++;
    if (*s == 0)
      break;
    if (*s == '/') {
      *s = 0;
      make_directory(dir);
      *s = '/';
    }
    s++;
  }

  free(dir);
}

void output_plain_content(FILE *output, char *content, int shortenp) {
  int length = 100;
  int c;

  if (! shortenp)
    length = strlen(content);

  // Output only the first 100 characters, but don't stop outputting
  // until we're at the end of word.
  while ((c = *content++) &&
	 (length-- > 0 || c != ' ')) {
    // Quote HTML special characters.
    if (c == '<')
      fprintf(output, "&lt;");
    else if (c == '>')
      fprintf(output, "&gt;");
    else if (c == '&')
      fprintf(output, "&amp;");
    else
      fprintf(output, "%c", c);
  }
}

void output_html_content(FILE *output, char *content, int shortenp) {
  int length = 100;
  char *end = content;

  if (! shortenp)
    length = strlen(content);

  // Output only the first 100 characters, but don't stop outputting
  // until we're at the end of word.
  while (*end && length > 0) {
    // Strip all HTML tags.
    while (*end && *end != '<')
      end++;
    *end++ = 0;
    if (end - content > length) {
      // Find the end of the current word.
      while (*(content + length) && *(content + length) != ' ')
	length++;
      *(content + length) = 0;
    }
    fputs(content, output);
    length -= end - content;
    while (*end && *end != '>')
      end++;
    end = content = end + 1;
  }
}

char *convert_to_utf8(const char *string, const char *charset) {
  const char *utf8, *local;
  iconv_t local_to_utf8;
  char *result;

  utf8 = g_mime_charset_name("utf-8");
  local = g_mime_charset_name(charset);
  local_to_utf8 = iconv_open(utf8, local);
  result = g_mime_iconv_strdup(local_to_utf8, string);

  return result;
}

void transform_simple_part(FILE *output, GMimePart* part, int shortenp) {
  GMimeContentType* ct = 0;
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
    charset = "iso-8859-1";

  for (p = content_type; *p; p++) 
    *p = tolower(*p);

  /* Get the content of the part. */
  wrapper = g_mime_part_get_content_object(part);
  stream = g_mime_data_wrapper_get_stream(wrapper);
  contentLen = g_mime_stream_length(stream);
  mcontent = calloc(contentLen + 1, 1);
  g_mime_stream_read(stream, mcontent, contentLen);
  use_content = mcontent;
  g_object_unref(wrapper);

  /* Convert contents to utf-8.  If the conversion wasn't successful,
     we use the original contents. */
  if (strcmp(charset, "utf-8")) {
    ccontent = convert_to_utf8(mcontent, charset);
    if (ccontent)
      use_content = ccontent;
  }

  fprintf(output, "<div class=body>");

  if (! strcmp(content_type, "text/html"))
    output_html_content(output, use_content, shortenp);
  else
    output_plain_content(output, use_content, shortenp);

  if (shortenp)
    fprintf(output, "...\n");
  fprintf(output, "</div>\n");

  free(mcontent);
  if (ccontent != NULL)
    free(ccontent);
}

void transform_part(FILE *output, GMimeObject *mime_part, int shortenp);

void transform_multipart(FILE *output, GMimeMultipart *mime_part,
			 int shortenp) {
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

    transform_part(output, preferred, shortenp);
  } else {
    /* Multipart mixed and related. */
    if (number_of_children > 1)
      transform_part(output, g_mime_multipart_get_part(mime_part, 0), shortenp);
  }
}

void transform_part(FILE *output, GMimeObject *mime_part, int shortenp) {
  if (GMIME_IS_MESSAGE_PART(mime_part)) {
    GMimeMessagePart *msgpart = GMIME_MESSAGE_PART(mime_part);
    GMimeMessage *msg = g_mime_message_part_get_message(msgpart);
    transform_part(output,  msg->mime_part, shortenp);
    g_object_unref(msg);
  } else if (GMIME_IS_MULTIPART(mime_part)) {
    transform_multipart(output, GMIME_MULTIPART(mime_part), shortenp); 
  } else {
    transform_simple_part(output, GMIME_PART(mime_part), shortenp);
  }
}

char *clean_from(char *from) {
  InternetAddress *iaddr;
  InternetAddressList *iaddr_list;

  if ((iaddr_list = internet_address_list_parse_string(from)) != NULL) {
    iaddr = internet_address_list_get_address(iaddr_list, 0);
    if (iaddr->name != NULL)
      strcpy(from, g_mime_utils_header_decode_text(iaddr->name));
    g_object_unref(iaddr_list);
  }
  return from;
}

void read_file(FILE *output, int input) {
  GMimeStream *stream;
  GMimeMessage *msg;
  time_t time;
  int tz;
  char *from;
  const char *archive;
  struct tm *tmm;
  char date[256];
  
  stream = g_mime_stream_fs_new(input);
  msg = g_mime_parser_construct_message(g_mime_parser_new_with_stream(stream));

  g_mime_message_get_date(msg, &time, &tz);
  from = clean_from(strdup(g_mime_object_get_header((GMimeObject*)msg,
						    "From")));
  archive = g_mime_object_get_header((GMimeObject*)msg, "Archived-at");

  fprintf(output, "<div class=article>\n");
  fprintf(output, "<span class=from>%s</span>\n", from);
  free(from);

  tmm = gmtime(&time);
  strftime(date, 255, "%Y-%m-%d %H:%M:%S", tmm);
  fprintf(output, "<span class=date>%s</span>\n", date);
  
  transform_part(output, msg->mime_part, archive? 1: 0); 

  if (archive) {
    char *final, *garchive = strdup(archive);
    while (*archive &&
	   (*archive == '<' ||
	    *archive == ' '))
      archive++;
    final = strrchr(archive, '>');
    if (final)
      *final = 0;
    fprintf(output, "<a href=\"%s\">Read more</a>\n", archive);
    free(garchive);
  }
  fprintf(output, "</div>\n");
  
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
  g_mime_init(GMIME_ENABLE_RFC2047_WORKAROUNDS);
  
  if (argc < 3) {
    printf("Usage: woof <output-file> <input-files...>\n");
    exit(-1);
  }

  // Write to a temporary file to avoid having clients see
  // a half-written file.
  output_name = argv[1];
  ensure_directory(output_name);
  tmp_name = malloc(strlen(output_name) + 5);
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
  
  exit(0);
}
