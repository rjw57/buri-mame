// license:BSD-3-Clause
// copyright-holders:Aaron Giles
/***************************************************************************

    xmlfile.c

    XML file parsing code.

***************************************************************************/

#include <assert.h>

#include "xmlfile.h"
#include <ctype.h>
#include <expat.h>


/***************************************************************************
    CONSTANTS
***************************************************************************/

#define TEMP_BUFFER_SIZE        4096



/***************************************************************************
    TYPE DEFINITIONS
***************************************************************************/

struct xml_parse_info
{
	XML_Parser          parser;
	xml_data_node *     rootnode;
	xml_data_node *     curnode;
	uint32_t              flags;
};



/***************************************************************************
    PROTOTYPES
***************************************************************************/

/* expat interfaces */
static bool expat_setup_parser(xml_parse_info *parse_info, xml_parse_options *opts);
static void expat_element_start(void *data, const XML_Char *name, const XML_Char **attributes);
static void expat_data(void *data, const XML_Char *s, int len);
static void expat_element_end(void *data, const XML_Char *name);

/* node/attributes additions */
static xml_data_node *add_child(xml_data_node *parent, const char *name, const char *value);
static xml_attribute_node *add_attribute(xml_data_node *node, const char *name, const char *value);

/* recursive tree operations */
static void write_node_recursive(xml_data_node *node, int indent, util::core_file &file);
static void free_node_recursive(xml_data_node *node);



/***************************************************************************
    CORE IMPLEMENTATION
***************************************************************************/

/*-------------------------------------------------
    copystring - make an allocated copy of a
    string
-------------------------------------------------*/

static const char *copystring(const char *input)
{
	char *newstr;

	/* nullptr just passes through */
	if (input == nullptr)
		return nullptr;

	/* make a lower-case copy if the allocation worked */
	newstr = (char *)malloc(strlen(input) + 1);
	if (newstr != nullptr)
		strcpy(newstr, input);

	return newstr;
}


/*-------------------------------------------------
    copystring_lower - make an allocated copy of
    a string and convert it to lowercase along
    the way
-------------------------------------------------*/

static const char *copystring_lower(const char *input)
{
	char *newstr;
	int i;

	/* nullptr just passes through */
	if (input == nullptr)
		return nullptr;

	/* make a lower-case copy if the allocation worked */
	newstr = (char *)malloc(strlen(input) + 1);
	if (newstr != nullptr)
	{
		for (i = 0; input[i] != 0; i++)
			newstr[i] = tolower((uint8_t)input[i]);
		newstr[i] = 0;
	}

	return newstr;
}



/***************************************************************************
    XML FILE OBJECTS
***************************************************************************/

/*-------------------------------------------------
    xml_file_create - create a new xml file
    object
-------------------------------------------------*/

xml_data_node *xml_file_create(void)
{
	xml_data_node *rootnode;

	/* create a root node */
	rootnode = (xml_data_node *)malloc(sizeof(*rootnode));
	if (rootnode == nullptr)
		return nullptr;
	memset(rootnode, 0, sizeof(*rootnode));
	return rootnode;
}


/*-------------------------------------------------
    xml_file_read - parse an XML file into its
    nodes
-------------------------------------------------*/

xml_data_node *xml_file_read(util::core_file &file, xml_parse_options *opts)
{
	xml_parse_info parse_info;
	int done;

	/* set up the parser */
	if (!expat_setup_parser(&parse_info, opts))
		return nullptr;

	/* loop through the file and parse it */
	do
	{
		char tempbuf[TEMP_BUFFER_SIZE];

		/* read as much as we can */
		int bytes = file.read(tempbuf, sizeof(tempbuf));
		done = file.eof();

		/* parse the data */
		if (XML_Parse(parse_info.parser, tempbuf, bytes, done) == XML_STATUS_ERROR)
		{
			if (opts != nullptr && opts->error != nullptr)
			{
				opts->error->error_message = XML_ErrorString(XML_GetErrorCode(parse_info.parser));
				opts->error->error_line = XML_GetCurrentLineNumber(parse_info.parser);
				opts->error->error_column = XML_GetCurrentColumnNumber(parse_info.parser);
			}

			xml_file_free(parse_info.rootnode);
			XML_ParserFree(parse_info.parser);
			return nullptr;
		}

	} while (!done);

	/* free the parser */
	XML_ParserFree(parse_info.parser);

	/* return the root node */
	return parse_info.rootnode;
}


/*-------------------------------------------------
    xml_string_read - parse an XML string into its
    nodes
-------------------------------------------------*/

xml_data_node *xml_string_read(const char *string, xml_parse_options *opts)
{
	xml_parse_info parse_info;
	int length = (int)strlen(string);

	/* set up the parser */
	if (!expat_setup_parser(&parse_info, opts))
		return nullptr;

	/* parse the data */
	if (XML_Parse(parse_info.parser, string, length, 1) == XML_STATUS_ERROR)
	{
		if (opts != nullptr && opts->error != nullptr)
		{
			opts->error->error_message = XML_ErrorString(XML_GetErrorCode(parse_info.parser));
			opts->error->error_line = XML_GetCurrentLineNumber(parse_info.parser);
			opts->error->error_column = XML_GetCurrentColumnNumber(parse_info.parser);
		}

		xml_file_free(parse_info.rootnode);
		XML_ParserFree(parse_info.parser);
		return nullptr;
	}

	/* free the parser */
	XML_ParserFree(parse_info.parser);

	/* return the root node */
	return parse_info.rootnode;
}


/*-------------------------------------------------
    xml_file_write - write an XML tree to a file
-------------------------------------------------*/

void xml_file_write(xml_data_node *node, util::core_file &file)
{
	/* ensure this is a root node */
	if (node->name != nullptr)
		return;

	/* output a simple header */
	file.printf("<?xml version=\"1.0\"?>\n");
	file.printf("<!-- This file is autogenerated; comments and unknown tags will be stripped -->\n");

	/* loop over children of the root and output */
	for (node = node->child; node; node = node->next)
		write_node_recursive(node, 0, file);
}


/*-------------------------------------------------
    xml_file_free - free an XML file object
-------------------------------------------------*/

void xml_file_free(xml_data_node *node)
{
	/* ensure this is a root node */
	if (node->name != nullptr)
		return;

	free_node_recursive(node);
}



/***************************************************************************
    XML NODE MANAGEMENT
***************************************************************************/

/*-------------------------------------------------
    xml_count_children - count the number of
    child nodes
-------------------------------------------------*/

int xml_count_children(xml_data_node *node)
{
	int count = 0;

	/* loop over children and count */
	for (node = node->child; node; node = node->next)
		count++;
	return count;
}


/*-------------------------------------------------
    xml_get_sibling - find the next sibling of
    the specified node with the specified tag
-------------------------------------------------*/

xml_data_node *xml_get_sibling(xml_data_node *node, const char *name)
{
	/* loop over siblings and find a matching name */
	for ( ; node; node = node->next)
		if (strcmp(node->name, name) == 0)
			return node;
	return nullptr;
}


/*-------------------------------------------------
    xml_find_matching_sibling - find the next
    sibling of the specified node with the
    specified tag or attribute/value pair
-------------------------------------------------*/

xml_data_node *xml_find_matching_sibling(xml_data_node *node, const char *name, const char *attribute, const char *matchval)
{
	/* loop over siblings and find a matching attribute */
	for ( ; node; node = node->next)
	{
		/* can pass nullptr as a wildcard for the node name */
		if (name == nullptr || strcmp(name, node->name) == 0)
		{
			/* find a matching attribute */
			xml_attribute_node *attr = xml_get_attribute(node, attribute);
			if (attr != nullptr && strcmp(attr->value, matchval) == 0)
				return node;
		}
	}
	return nullptr;
}


/*-------------------------------------------------
    xml_add_child - add a new child node to the
    given node
-------------------------------------------------*/

xml_data_node *xml_add_child(xml_data_node *node, const char *name, const char *value)
{
	/* just a standard add child */
	return add_child(node, name, value);
}


/*-------------------------------------------------
    xml_get_or_add_child - find a child node of
    the specified type; if not found, add one
-------------------------------------------------*/

xml_data_node *xml_get_or_add_child(xml_data_node *node, const char *name, const char *value)
{
	xml_data_node *child;

	/* find the child first */
	child = xml_get_sibling(node->child, name);
	if (child != nullptr)
		return child;

	/* if not found, do a standard add child */
	return add_child(node, name, value);
}


/*-------------------------------------------------
    xml_delete_node - delete a node and its
    children
-------------------------------------------------*/

void xml_delete_node(xml_data_node *node)
{
	xml_data_node **pnode;

	/* first unhook us from the list of children of our parent */
	for (pnode = &node->parent->child; *pnode; pnode = &(*pnode)->next)
		if (*pnode == node)
		{
			*pnode = node->next;
			break;
		}

	/* now free ourselves and our children */
	free_node_recursive(node);
}



/***************************************************************************
    XML ATTRIBUTE MANAGEMENT
***************************************************************************/

/*-------------------------------------------------
    xml_get_attribute - get the value of the
    specified attribute, or nullptr if not found
-------------------------------------------------*/

xml_attribute_node *xml_get_attribute(xml_data_node *node, const char *attribute)
{
	xml_attribute_node *anode;

	/* loop over attributes and find a match */
	for (anode = node->attribute; anode; anode = anode->next)
		if (strcmp(anode->name, attribute) == 0)
			return anode;
	return nullptr;
}


/*-------------------------------------------------
    xml_get_attribute_string - get the string
    value of the specified attribute; if not
    found, return = the provided default
-------------------------------------------------*/

const char *xml_get_attribute_string(xml_data_node *node, const char *attribute, const char *defvalue)
{
	xml_attribute_node *attr = xml_get_attribute(node, attribute);
	return attr ? attr->value : defvalue;
}


/*-------------------------------------------------
    xml_get_attribute_int - get the integer
    value of the specified attribute; if not
    found, return = the provided default
-------------------------------------------------*/

int xml_get_attribute_int(xml_data_node *node, const char *attribute, int defvalue)
{
	const char *string = xml_get_attribute_string(node, attribute, nullptr);
	int value;
	unsigned int uvalue;

	if (string == nullptr)
		return defvalue;
	if (string[0] == '$')
		return (sscanf(&string[1], "%X", &uvalue) == 1) ? uvalue : defvalue;
	if (string[0] == '0' && string[1] == 'x')
		return (sscanf(&string[2], "%X", &uvalue) == 1) ? uvalue : defvalue;
	if (string[0] == '#')
		return (sscanf(&string[1], "%d", &value) == 1) ? value : defvalue;
	return (sscanf(&string[0], "%d", &value) == 1) ? value : defvalue;
}


/*-------------------------------------------------
    xml_get_attribute_int_format - return the
    format of the given integer attribute
-------------------------------------------------*/

int xml_get_attribute_int_format(xml_data_node *node, const char *attribute)
{
	const char *string = xml_get_attribute_string(node, attribute, nullptr);

	if (string == nullptr)
		return XML_INT_FORMAT_DECIMAL;
	if (string[0] == '$')
		return XML_INT_FORMAT_HEX_DOLLAR;
	if (string[0] == '0' && string[1] == 'x')
		return XML_INT_FORMAT_HEX_C;
	if (string[0] == '#')
		return XML_INT_FORMAT_DECIMAL_POUND;
	return XML_INT_FORMAT_DECIMAL;
}


/*-------------------------------------------------
    xml_get_attribute_float - get the float
    value of the specified attribute; if not
    found, return = the provided default
-------------------------------------------------*/

/**
 * @fn  float xml_get_attribute_float(xml_data_node *node, const char *attribute, float defvalue)
 *
 * @brief   XML get attribute float.
 *
 * @param [in,out]  node    If non-null, the node.
 * @param   attribute       The attribute.
 * @param   defvalue        The defvalue.
 *
 * @return  A float.
 */

float xml_get_attribute_float(xml_data_node *node, const char *attribute, float defvalue)
{
	const char *string = xml_get_attribute_string(node, attribute, nullptr);
	float value;

	if (string == nullptr || sscanf(string, "%f", &value) != 1)
		return defvalue;
	return value;
}


/*-------------------------------------------------
    xml_set_attribute - set a new attribute and
    string value on the node
-------------------------------------------------*/

/**
 * @fn  xml_attribute_node *xml_set_attribute(xml_data_node *node, const char *name, const char *value)
 *
 * @brief   XML set attribute.
 *
 * @param [in,out]  node    If non-null, the node.
 * @param   name            The name.
 * @param   value           The value.
 *
 * @return  null if it fails, else an xml_attribute_node*.
 */

xml_attribute_node *xml_set_attribute(xml_data_node *node, const char *name, const char *value)
{
	xml_attribute_node *anode;

	/* first find an existing one to replace */
	anode = xml_get_attribute(node, name);

	/* if we found it, free the old value and replace it */
	if (anode != nullptr)
	{
		if (anode->value != nullptr)
			free((void *)anode->value);
		anode->value = copystring(value);
	}

	/* otherwise, create a new node */
	else
		anode = add_attribute(node, name, value);

	return anode;
}


/*-------------------------------------------------
    xml_set_attribute_int - set a new attribute and
    integer value on the node
-------------------------------------------------*/

/**
 * @fn  xml_attribute_node *xml_set_attribute_int(xml_data_node *node, const char *name, int value)
 *
 * @brief   XML set attribute int.
 *
 * @param [in,out]  node    If non-null, the node.
 * @param   name            The name.
 * @param   value           The value.
 *
 * @return  null if it fails, else an xml_attribute_node*.
 */

xml_attribute_node *xml_set_attribute_int(xml_data_node *node, const char *name, int value)
{
	char buffer[100];
	sprintf(buffer, "%d", value);
	return xml_set_attribute(node, name, buffer);
}


/*-------------------------------------------------
    xml_set_attribute_int - set a new attribute and
    float value on the node
-------------------------------------------------*/

/**
 * @fn  xml_attribute_node *xml_set_attribute_float(xml_data_node *node, const char *name, float value)
 *
 * @brief   XML set attribute float.
 *
 * @param [in,out]  node    If non-null, the node.
 * @param   name            The name.
 * @param   value           The value.
 *
 * @return  null if it fails, else an xml_attribute_node*.
 */

xml_attribute_node *xml_set_attribute_float(xml_data_node *node, const char *name, float value)
{
	char buffer[100];
	sprintf(buffer, "%f", (double) value);
	return xml_set_attribute(node, name, buffer);
}



/***************************************************************************
    MISCELLANEOUS INTERFACES
***************************************************************************/

/*-------------------------------------------------
    xml_normalize_string - normalize a string
    to ensure it doesn't contain embedded tags
-------------------------------------------------*/

/**
 * @fn  const char *xml_normalize_string(const char *string)
 *
 * @brief   XML normalize string.
 *
 * @param   string  The string.
 *
 * @return  null if it fails, else a char*.
 */

const char *xml_normalize_string(const char *string)
{
	static char buffer[1024];
	char *d = &buffer[0];

	if (string != nullptr)
	{
		while (*string)
		{
			switch (*string)
			{
				case '\"' : d += sprintf(d, "&quot;"); break;
				case '&'  : d += sprintf(d, "&amp;"); break;
				case '<'  : d += sprintf(d, "&lt;"); break;
				case '>'  : d += sprintf(d, "&gt;"); break;
				default:
					*d++ = *string;
			}
			++string;
		}
	}
	*d++ = 0;
	return buffer;
}



/***************************************************************************
    EXPAT INTERFACES
***************************************************************************/

/*-------------------------------------------------
    expat_malloc/expat_realloc/expat_free -
    wrappers for memory allocation functions so
    that they pass through out memory tracking
    systems
-------------------------------------------------*/

/**
 * @fn  static void *expat_malloc(size_t size)
 *
 * @brief   Expat malloc.
 *
 * @param   size    The size.
 *
 * @return  null if it fails, else a void*.
 */

static void *expat_malloc(size_t size)
{
	uint32_t *result = (uint32_t *)malloc(size + 4 * sizeof(uint32_t));
	*result = size;
	return &result[4];
}

/**
 * @fn  static void expat_free(void *ptr)
 *
 * @brief   Expat free.
 *
 * @param [in,out]  ptr If non-null, the pointer.
 */

static void expat_free(void *ptr)
{
	if (ptr != nullptr)
		free(&((uint32_t *)ptr)[-4]);
}

/**
 * @fn  static void *expat_realloc(void *ptr, size_t size)
 *
 * @brief   Expat realloc.
 *
 * @param [in,out]  ptr If non-null, the pointer.
 * @param   size        The size.
 *
 * @return  null if it fails, else a void*.
 */

static void *expat_realloc(void *ptr, size_t size)
{
	void *newptr = expat_malloc(size);
	if (newptr == nullptr)
		return nullptr;
	if (ptr != nullptr)
	{
		uint32_t oldsize = ((uint32_t *)ptr)[-4];
		memcpy(newptr, ptr, oldsize);
		expat_free(ptr);
	}
	return newptr;
}


/*-------------------------------------------------
    expat_setup_parser - set up expat for parsing
-------------------------------------------------*/

/**
 * @fn  static int expat_setup_parser(xml_parse_info *parse_info, xml_parse_options *opts)
 *
 * @brief   Expat setup parser.
 *
 * @param [in,out]  parse_info  If non-null, information describing the parse.
 * @param [in,out]  opts        If non-null, options for controlling the operation.
 *
 * @return  An int.
 */

static bool expat_setup_parser(xml_parse_info *parse_info, xml_parse_options *opts)
{
	XML_Memory_Handling_Suite memcallbacks;

	/* setup parse_info structure */
	memset(parse_info, 0, sizeof(*parse_info));
	if (opts != nullptr)
	{
		parse_info->flags = opts->flags;
		if (opts->error != nullptr)
		{
			opts->error->error_message = nullptr;
			opts->error->error_line = 0;
			opts->error->error_column = 0;
		}
	}

	/* create a root node */
	parse_info->rootnode = xml_file_create();
	if (parse_info->rootnode == nullptr)
		return false;
	parse_info->curnode = parse_info->rootnode;

	/* create the XML parser */
	memcallbacks.malloc_fcn = expat_malloc;
	memcallbacks.realloc_fcn = expat_realloc;
	memcallbacks.free_fcn = expat_free;
	parse_info->parser = XML_ParserCreate_MM(nullptr, &memcallbacks, nullptr);
	if (parse_info->parser == nullptr)
	{
		free(parse_info->rootnode);
		return false;
	}

	/* configure the parser */
	XML_SetElementHandler(parse_info->parser, expat_element_start, expat_element_end);
	XML_SetCharacterDataHandler(parse_info->parser, expat_data);
	XML_SetUserData(parse_info->parser, parse_info);

	/* optional parser initialization step */
	if (opts != nullptr && opts->init_parser != nullptr)
		(*opts->init_parser)(parse_info->parser);
	return true;
}


/*-------------------------------------------------
    expat_element_start - expat callback for a new
    element
-------------------------------------------------*/

/**
 * @fn  static void expat_element_start(void *data, const XML_Char *name, const XML_Char **attributes)
 *
 * @brief   Expat element start.
 *
 * @param [in,out]  data    If non-null, the data.
 * @param   name            The name.
 * @param   attributes      The attributes.
 */

static void expat_element_start(void *data, const XML_Char *name, const XML_Char **attributes)
{
	xml_parse_info *parse_info = (xml_parse_info *) data;
	xml_data_node **curnode = &parse_info->curnode;
	xml_data_node *newnode;
	int attr;

	/* add a new child node to the current node */
	newnode = add_child(*curnode, name, nullptr);
	if (newnode == nullptr)
		return;

	/* remember the line number */
	newnode->line = XML_GetCurrentLineNumber(parse_info->parser);

	/* add all the attributes as well */
	for (attr = 0; attributes[attr]; attr += 2)
		add_attribute(newnode, attributes[attr+0], attributes[attr+1]);

	/* set us up as the current node */
	*curnode = newnode;
}


/*-------------------------------------------------
    expat_data - expat callback for an additional
    element data
-------------------------------------------------*/

/**
 * @fn  static void expat_data(void *data, const XML_Char *s, int len)
 *
 * @brief   Expat data.
 *
 * @param [in,out]  data    If non-null, the data.
 * @param   s               The const XML_Char * to process.
 * @param   len             The length.
 */

static void expat_data(void *data, const XML_Char *s, int len)
{
	xml_parse_info *parse_info = (xml_parse_info *) data;
	xml_data_node **curnode = &parse_info->curnode;
	int oldlen = 0;
	char *newdata;

	/* if no data, skip */
	if (len == 0)
		return;

	/* determine how much data we currently have */
	if ((*curnode)->value != nullptr)
		oldlen = (int)strlen((*curnode)->value);

	/* realloc */
	newdata = (char *)malloc(oldlen + len + 1);
	if (newdata == nullptr)
		return;
	if ((*curnode)->value != nullptr)
	{
		memcpy(newdata, (*curnode)->value, oldlen);
		free((void *)(*curnode)->value);
	}
	(*curnode)->value = newdata;

	/* copy in the new data a NULL-terminate */
	memcpy(&newdata[oldlen], s, len);
	newdata[oldlen + len] = 0;
	(*curnode)->value = newdata;
}


/*-------------------------------------------------
    expat_element_end - expat callback for the end
    of an element
-------------------------------------------------*/

/**
 * @fn  static void expat_element_end(void *data, const XML_Char *name)
 *
 * @brief   Expat element end.
 *
 * @param [in,out]  data    If non-null, the data.
 * @param   name            The name.
 */

static void expat_element_end(void *data, const XML_Char *name)
{
	xml_parse_info *parse_info = (xml_parse_info *) data;
	xml_data_node **curnode = &parse_info->curnode;
	char *orig;

	/* strip leading/trailing spaces from the value data */
	orig = (char *)(*curnode)->value;
	if (orig != nullptr && !(parse_info->flags & XML_PARSE_FLAG_WHITESPACE_SIGNIFICANT))
	{
		char *start = orig;
		char *end = start + strlen(start);

		/* first strip leading spaces */
		while (*start && isspace((uint8_t)*start))
			start++;

		/* then strip trailing spaces */
		while (end > start && isspace((uint8_t)end[-1]))
			end--;

		/* if nothing left, just free it */
		if (start == end)
		{
			free(orig);
			(*curnode)->value = nullptr;
		}

		/* otherwise, memmove the data */
		else
		{
			memmove(orig, start, end - start);
			orig[end - start] = 0;
		}
	}

	/* back us up a node */
	*curnode = (*curnode)->parent;
}



/***************************************************************************
    NODE/ATTRIBUTE ADDITIONS
***************************************************************************/

/*-------------------------------------------------
    add_child - add a new node to the parent
-------------------------------------------------*/

/**
 * @fn  static xml_data_node *add_child(xml_data_node *parent, const char *name, const char *value)
 *
 * @brief   Adds a child.
 *
 * @param [in,out]  parent  If non-null, the parent.
 * @param   name            The name.
 * @param   value           The value.
 *
 * @return  null if it fails, else an xml_data_node*.
 */

static xml_data_node *add_child(xml_data_node *parent, const char *name, const char *value)
{
	xml_data_node **pnode;
	xml_data_node *node;

	/* new element: create a new node */
	node = (xml_data_node *)malloc(sizeof(*node));
	if (node == nullptr)
		return nullptr;

	/* initialize the members */
	node->next = nullptr;
	node->parent = parent;
	node->child = nullptr;
	node->name = copystring_lower(name);
	if (node->name == nullptr)
	{
		free(node);
		return nullptr;
	}
	node->value = copystring(value);
	if (node->value == nullptr && value != nullptr)
	{
		free((void *)node->name);
		free(node);
		return nullptr;
	}
	node->attribute = nullptr;

	/* add us to the end of the list of siblings */
	for (pnode = &parent->child; *pnode; pnode = &(*pnode)->next) { }
	*pnode = node;

	return node;
}


/*-------------------------------------------------
    add_attribute - add a new attribute to the
    given node
-------------------------------------------------*/

/**
 * @fn  static xml_attribute_node *add_attribute(xml_data_node *node, const char *name, const char *value)
 *
 * @brief   Adds an attribute.
 *
 * @param [in,out]  node    If non-null, the node.
 * @param   name            The name.
 * @param   value           The value.
 *
 * @return  null if it fails, else an xml_attribute_node*.
 */

static xml_attribute_node *add_attribute(xml_data_node *node, const char *name, const char *value)
{
	xml_attribute_node *anode, **panode;

	/* allocate a new attribute node */
	anode = (xml_attribute_node *)malloc(sizeof(*anode));
	if (anode == nullptr)
		return nullptr;

	/* fill it in */
	anode->next = nullptr;
	anode->name = copystring_lower(name);
	if (anode->name == nullptr)
	{
		free(anode);
		return nullptr;
	}
	anode->value = copystring(value);
	if (anode->value == nullptr)
	{
		free((void *)anode->name);
		free(anode);
		return nullptr;
	}

	/* add us to the end of the list of attributes */
	for (panode = &node->attribute; *panode; panode = &(*panode)->next) { }
	*panode = anode;

	return anode;
}



/***************************************************************************
    RECURSIVE TREE OPERATIONS
***************************************************************************/

/*-------------------------------------------------
    write_node_recursive - recursively write
    an XML node and its children to a file
-------------------------------------------------*/

/**
 * @fn  static void write_node_recursive(xml_data_node *node, int indent, util::core_file &file)
 *
 * @brief   Writes a node recursive.
 *
 * @param [in,out]  node    If non-null, the node.
 * @param   indent          The indent.
 * @param [in,out]  file    If non-null, the file.
 */

static void write_node_recursive(xml_data_node *node, int indent, util::core_file &file)
{
	xml_attribute_node *anode;
	xml_data_node *child;

	/* output this tag */
	file.printf("%*s<%s", indent, "", node->name);

	/* output any attributes */
	for (anode = node->attribute; anode; anode = anode->next)
		file.printf(" %s=\"%s\"", anode->name, anode->value);

	/* if there are no children and no value, end the tag here */
	if (node->child == nullptr && node->value == nullptr)
		file.printf(" />\n");

	/* otherwise, close this tag and output more stuff */
	else
	{
		file.printf(">\n");

		/* if there is a value, output that here */
		if (node->value != nullptr)
			file.printf("%*s%s\n", indent + 4, "", node->value);

		/* loop over children and output them as well */
		if (node->child != nullptr)
		{
			for (child = node->child; child; child = child->next)
				write_node_recursive(child, indent + 4, file);
		}

		/* write a closing tag */
		file.printf("%*s</%s>\n", indent, "", node->name);
	}
}


/*-------------------------------------------------
    free_node_recursive - recursively free
    the data allocated to an XML node
-------------------------------------------------*/

/**
 * @fn  static void free_node_recursive(xml_data_node *node)
 *
 * @brief   Free node recursive.
 *
 * @param [in,out]  node    If non-null, the node.
 */

static void free_node_recursive(xml_data_node *node)
{
	xml_attribute_node *anode, *nanode;
	xml_data_node *child, *nchild;

	/* free name/value */
	if (node->name != nullptr)
		free((void *)node->name);
	if (node->value != nullptr)
		free((void *)node->value);

	/* free attributes */
	for (anode = node->attribute; anode; anode = nanode)
	{
		/* free name/value */
		if (anode->name != nullptr)
			free((void *)anode->name);
		if (anode->value != nullptr)
			free((void *)anode->value);

		/* note the next node and free this node */
		nanode = anode->next;
		free(anode);
	}

	/* free the children */
	for (child = node->child; child; child = nchild)
	{
		/* note the next node and free this node */
		nchild = child->next;
		free_node_recursive(child);
	}

	/* finally free ourself */
	free(node);
}
