/*
 * Inc-Ex - Report redundant includes in C/C++ code
 * Copyright (c) 2011, Jonas Gehring
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the copyright holders nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <clang-c/Index.h>


/* Global options */
int verbosity = 0;


/* Include tree node */
struct itreenode {
	struct itreenode *parent;
	struct itreenode *first_child;
	struct itreenode *next; /* Next sibling */

	const char *path;
	int level;
	int refcount;
};

/* Baton for include_visitor() */
struct include_visitor_data {
	struct itreenode *root; /* Root node, to be allocated */
	struct itreenode *last; /* Last node, most likely a parent */
	int num_includes;
};

/* Baton for cursor_visitor() */
struct cursor_visitor_data {
	const char *source;
	struct itreenode **includes;
	int num_includes;
	enum CXLanguageKind language;
};


/* Searches for a treenode with the given path */
static struct itreenode *search_treenode(struct itreenode *root, const char *path)
{
	struct itreenode *node = NULL;
	if (!strcmp(root->path, path)) {
		return root;
	}
	if (root->next) {
		node = search_treenode(root->next, path);
	}
	if (node == NULL &&root->first_child) {
		node = search_treenode(root->first_child, path);
	}
	return node;
}

/* Adds another include to the include tree */
static void include_visitor(CXFile file, CXSourceLocation *stack, unsigned len, CXClientData baton)
{
	CXFile parent_file;
	unsigned line, column, offset;
	const char *parent_path;
	struct itreenode *node, *parent_node;

	const char *path = clang_getCString(clang_getFileName(file));
	struct include_visitor_data *data = (struct include_visitor_data *)baton;

	if (len == 0) { /* New root node */
		assert(data->root == NULL);
		data->root = calloc(1, sizeof(struct itreenode));
		data->root->path = strdup(path);
		data->last = data->root;
		data->num_includes++;
		if (verbosity > 0) {
			printf("#include %s\n", data->root->path);
		}
		return;
	}

	/* Process each header only once */
	/* TODO: Speed this up! */
	if ((node = search_treenode(data->root, path)) != NULL) {
		data->last = node;
		if (verbosity > 3) {
			printf("SKIP: #include %s\n", path);
		}
		return;
	}

	/* Insert new child node */
	data->num_includes++;

	assert(data->root != NULL);

	/* Determine parent */
	clang_getInstantiationLocation(*stack, &parent_file, &line, &column, &offset);
	parent_path = clang_getCString(clang_getFileName(parent_file));

	/* Search upwards for parent, we're visiting a stack here */
	parent_node = data->last;
	while (parent_node && strcmp(parent_node->path, parent_path)) {
		parent_node = parent_node->parent;
	}

	/* Thanks to node skipping, upwards is not the only way */
	if (parent_node == NULL) {
		parent_node = search_treenode(data->root, parent_path);
	}
	if (parent_node == NULL) {
		parent_node = data->root;
	}

	/* Allocate new node */
	node = calloc(1, sizeof(struct itreenode));
	node->path = strdup(path);
	node->level = parent_node->level + 1;
	node->parent = parent_node;

	/* Insert child node in parent list */
	if (parent_node->first_child == NULL) {
		parent_node->first_child = node;
	} else {
		struct itreenode *sibling = parent_node->first_child;
		while (sibling->next) {
			sibling = sibling->next;
		}
		sibling->next = node;
	}

	if (verbosity > 0) {
		printf("#include %s from %s\n", path, parent_node->path);
	}
	data->last = node;
}


/* Recursively build flat includes list */
static void fill_include_list(struct itreenode *node, struct cursor_visitor_data *data)
{
	data->includes[data->num_includes++] = node;

	if (node->next) {
		fill_include_list(node->next, data);
	}
	if (node->first_child) {
		fill_include_list(node->first_child, data);
	}
}

/* Compare two include nodes by path */
static int include_path_compare(const void *p1, const void *p2)
{
	return strcmp((*(struct itreenode **)p1)->path, (*(struct itreenode **)p2)->path);
}

/* Callback for AST traversing */
static enum CXChildVisitResult cursor_visitor(CXCursor cursor, CXCursor parent, CXClientData baton)
{
	CXFile file;
	unsigned line, column, offset;
	const char *path;

	CXSourceLocation ref_location = clang_getNullLocation();
	struct cursor_visitor_data *data = (struct cursor_visitor_data *)baton;
	enum CXCursorKind kind = clang_getCursorKind(cursor);

	/* Only process cursors in the source file */
	clang_getInstantiationLocation(clang_getCursorLocation(cursor), &file, &line, &column, &offset);
	path = clang_getCString(clang_getFileName(file));
	if (path == NULL || strcmp(data->source, path)) {
		if (verbosity > 3) {
			printf("SKIP: %s from %s [%s]\n", clang_getCString(clang_getCursorSpelling(cursor)), clang_getCString(clang_getFileName(file)), clang_getCString(clang_getCursorKindSpelling(kind)));
		}
		return CXChildVisit_Recurse;
	}

	/* Check language.
	 * The enumeration is sorted C -> Obj-C -> C++, so use the maximum value. */
	if (clang_getCursorLanguage(cursor) > data->language) {
		data->language = clang_getCursorLanguage(cursor);
	}

	/* For declarations and instantiatons, lookup definition
	 * and increase reference count of the corresponding location */
	if (kind == CXCursor_DeclRefExpr ||
		kind == CXCursor_TypeRef ||
  		kind == CXCursor_CallExpr ||
		kind == CXCursor_MacroInstantiation) {
		ref_location = clang_getCursorLocation(clang_getCursorReferenced(cursor));
	}

	if (clang_equalLocations(ref_location, clang_getNullLocation())) {
		if (verbosity > 3) {
			printf("SKIP: %s from %s [%s]\n", clang_getCString(clang_getCursorSpelling(cursor)), clang_getCString(clang_getFileName(file)), clang_getCString(clang_getCursorKindSpelling(kind)));
		}
		return CXChildVisit_Recurse;
	}

	if (verbosity > 3) {
		printf("OK : %s from %s [%s]\n", clang_getCString(clang_getCursorSpelling(cursor)), clang_getCString(clang_getFileName(file)), clang_getCString(clang_getCursorKindSpelling(kind)));
	}

	clang_getInstantiationLocation(ref_location, &file, &line, &column, &offset);
	path = clang_getCString(clang_getFileName(file));
	if (path) {
		struct itreenode **node, *keyptr, key;
		key.path = path;
		keyptr = &key;
		node = bsearch(&keyptr, data->includes, data->num_includes, sizeof(struct itreenode *), &include_path_compare);
		if (node) {
			(*node)->refcount++;

			/* A little verbosity */
			if (verbosity > 2 || \
			   (verbosity > 1 && !strcmp(data->source, clang_getCString(clang_getFileName(file))))) {
				printf("%s from %s\n", clang_getCString(clang_getCursorSpelling(cursor)), (*node)->path);
			}
		}
	}
	return CXChildVisit_Recurse;
}

/* Sum up total reference count of node and children */
static int count_child_references(struct itreenode *node)
{
	struct itreenode *child = node->first_child;
	int n = 0;

	while (child) {
		n += child->refcount + count_child_references(child);
		child = child->next;
	}
	return n;
}

/* Prints all children with non-zero reference counts */
static void print_referenced_children(struct itreenode *node, const char *format)
{
	struct itreenode *child = node->first_child;

	while (child) {
		if (child->refcount > 0) {
			printf(format, child->path);
		}
		child = child->next;
	}
	if (node->first_child) {
		print_referenced_children(node->first_child, format);
	}
}

/* Checks if the given path has an extension */
static int has_extension(const char *path)
{
	char *ptr = strrchr(path, '.');
	if (ptr == NULL) {
		return 0;
	}
	return (strchr(ptr, '/') == NULL);
}

#ifndef NDEBUG

/* Prints reference count of includes */
static void print_reference_count(struct itreenode *node)
{
	printf("%d (%d) %s\n", node->refcount, count_child_references(node), node->path);

	if (node->next) {
		print_reference_count(node->next);
	}
	if (node->first_child) {
		print_reference_count(node->first_child);
	}
}

#endif /* NDEBUG */


/* Program entry point */
int main(int argc, char **argv)
{
	const char *source;
	int carg_start;
	CXIndex index;
	CXTranslationUnit unit;
	CXCursor cursor;
	struct include_visitor_data iv_data;
	struct cursor_visitor_data cv_data;
	struct itreenode *tmp_node;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s [-v*] [compile-args]\n", argv[0]);
		return EXIT_FAILURE;
	}

	/* Parse arguments */
	if (!strncmp(argv[1], "-v", 2)) {
		int pos = 1;
		while (argv[1][pos++] == 'v') {
			verbosity++;
		}
		carg_start = 2;
	} else {
		carg_start = 1;
	}

	/* Parse source file and setup cursor */
	index = clang_createIndex(1, 1);
	if (index == NULL) {
		fprintf(stderr, "Error creating index\n");
		return 1;
	}
	unit = clang_parseTranslationUnit(index, NULL, (const char * const *)argv+carg_start, argc-carg_start,  NULL, 0, CXTranslationUnit_DetailedPreprocessingRecord);
	if (unit == NULL) {
		fprintf(stderr, "Error parsing source file\n");
		return 1;
	}
	cursor = clang_getTranslationUnitCursor(unit);
	if (clang_isInvalid(clang_getCursorKind(cursor))) {
		fprintf(stderr, "Error requesting cursor\n");
		return 1;
	}

	source = clang_getCString(clang_getTranslationUnitSpelling(unit));
	printf("Parsed %s\n", source);

	/* Build include tree */
	iv_data.root = iv_data.last = NULL;
	iv_data.num_includes = 0;
	clang_getInclusions(unit, &include_visitor, &iv_data);
	if (iv_data.root == NULL) {
		return 1;
	}

	/* Build sorted list of includes for faster lookup during AST traversal */
	cv_data.includes = malloc(sizeof(struct itreenode *) * iv_data.num_includes);
	cv_data.num_includes = 0; /* Will be used as list index during fill */
	fill_include_list(iv_data.root, &cv_data);
	assert(cv_data.num_includes == iv_data.num_includes);
	qsort(cv_data.includes, cv_data.num_includes, sizeof(struct itreenode *), &include_path_compare);

	/* Traverse AST and count header references */
	cv_data.source = source;
	clang_visitChildren(cursor, &cursor_visitor, &cv_data);

	/* Finally, report direct included, unused headers */
	tmp_node = iv_data.root->first_child;
	while (tmp_node) {
		if (tmp_node->refcount == 0) {
			if (count_child_references(tmp_node) == 0) {
				printf("Unreferenced header: %s\n", tmp_node->path);
			} else {
				/* In C++, it's common to have indirect header files without extension */
				if (cv_data.language != CXLanguage_CPlusPlus || has_extension(tmp_node->path)) {
					printf("Indirectly referenced header: %s\n", tmp_node->path);
					print_referenced_children(tmp_node, "  [includes referenced header: %s]\n");
				}
			}
		}
		tmp_node = tmp_node->next;
	}

	/* Debug: print reference counts */
#ifndef NDEBUG
	if (verbosity > 4) {
		print_reference_count(iv_data.root->first_child);
	}
#endif

	/* End marker is useful when running with make(1) */
	printf("========================================================================\n");

	/* Cleanup clang data only, let the OS handle the rest */
	clang_disposeTranslationUnit(unit);
	return 0;
}
