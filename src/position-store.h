#pragma once

#include <glib.h>

G_BEGIN_DECLS

typedef struct _PositionStore PositionStore;

PositionStore *position_store_new    (const char *path);
void           position_store_free   (PositionStore *store);
gboolean       position_store_lookup (PositionStore *store, const char *name, int *x, int *y);
void           position_store_set    (PositionStore *store, const char *name, int x, int y);
void           position_store_remove (PositionStore *store, const char *name);
void           position_store_rename (PositionStore *store, const char *old_name, const char *new_name);
void           position_store_flush  (PositionStore *store);

G_END_DECLS
