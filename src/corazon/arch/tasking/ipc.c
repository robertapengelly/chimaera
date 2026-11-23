/******************************************************************************
 * @file            ipc.c
 *****************************************************************************/
#include    <corazon/tasking/ipc.h>
#include    <corazon/tasking/process.h>
#include    <corazon/utils/list.h>

#include    <stddef.h>
#include    <string.h>

static ipc_node_t root = {

    .name    = 0,        .type        = IPC_FOLDER, .data.folder = { 0 }, .owner = 0,
    .general = IPC_READ, .access_table = 0,         .parent      = 0

};

extern char *kstrdup (const char *s);
extern void *kmalloc (size_t size);

extern void kfree (void *ptr);

static IPC_ERROR create_node (ipc_node_t *parent, ipc_node_t **node, const char *name, IPC_TYPE type) {

    if (!parent) {
        return IPC_NULLPOINTER;
    }
    
    if (parent->type != IPC_FOLDER) {
        return IPC_WRONGTYPE;
    }
    
    if (!(*node = kmalloc (sizeof (ipc_node_t)))) {
        return IPC_NOTENOUGHMEMORY;
    }
    
    (*node)->name         = kstrdup (name);
    (*node)->type         = type;
    
    (*node)->owner        = thread_get_current ()->owner->pid;
    (*node)->general      = IPC_READ;
    
    (*node)->access_table = 0;
    (*node)->parent       = parent;
    
    memset (&(*node)->data, 0, sizeof ((*node)->data));
    list_append (&parent->data.folder, *node);
    
    return IPC_SUCCESSFUL;

}

static ipc_node_t *get_node (const char *remaining_path, ipc_node_t *node) {

    const char *end = strpbrk (remaining_path, "/|\\");
    list_node_t *e;
    
    if (node->type == IPC_FOLDER) {
    
        if (end == 0) {                 /* Final element */
        
            for (e = node->data.folder.head; e != 0; e = e->next) {
            
                ipc_node_t *newnode = e->value;
                
                if (strcmp (remaining_path, newnode->name) == 0) {
                    return newnode;
                }
            
            }
        } else {
        
            size_t sublength = end-remaining_path;
            
            for (e = node->data.folder.head; e != 0; e = e->next) {
            
                ipc_node_t *newnode = e->value;
                
                if (strncmp (remaining_path, newnode->name, sublength) == 0 && strlen (newnode->name) == sublength) {
                
                    if (newnode->type == IPC_FOLDER) {
                        return get_node (end + 1, newnode);                     /* One layer downwards */
                    }
                    
                    return 0;           /* Node with name found, but we cannot go one step downwards, because its no folder. -> Not found. */
                
                }
            
            }
        
        }
    
    }
    
    return 0;                           /* Not found */

}

static int access_allowed (ipc_node_t *node, IPC_RIGHTS needed) {

    uint32_t pid = thread_get_current ()->owner->pid;
    
    if (pid == 0 || pid == node->owner) {                   /* Kernel and owner have full access */
        return 1;
    }
    
    if (node->access_table) {
    
        list_node_t *e;
        
        for (e = node->access_table->head; e != 0; e = e->next) {
        
            ipc_certificate_t *certificate = e->value;
            
            if (certificate->owner == pid) {                /* Task has a specific certificate to access this node */
                return (certificate->privileges & needed) == needed;
            }
        
        }
    
    }
    
    return (node->general & needed) == needed;              /* Use general access rights as fallback */

}

static IPC_ERROR prepare_node_to_write (ipc_node_t **node, const char *path, IPC_TYPE type) {

    if (*node == 0) {
    
        IPC_ERROR err = ipc_create_node(path, node, type);
        
        if (err != IPC_SUCCESSFUL) {
            return err;
        }
    
    } else if ((*node)->type != type) {
        return IPC_WRONGTYPE;
    } else if (!access_allowed(*node, IPC_WRITE)) {
        return IPC_ACCESSDENIED;
    }
    
    return IPC_SUCCESSFUL;

}


IPC_ERROR ipc_create_node (const char *path, ipc_node_t **node, IPC_TYPE type) {

    ipc_node_t *parent, *nparent;
    
    char *npath, *nodename, *temp;
    size_t size;
    
    if (*path == '/' || *path == '|' || *path == '\\') {
        path++;
    }
    
    parent = &root;
    
    if (*path == '~') {
    
        parent = thread_get_current ()->owner->ipc_node;
        path += 2;
    
    } else if (*path == '>') {
    
        parent = thread_get_current ()->owner->ipc_working;
        path += 2;
    
    }
    
    size = strlen (path) + 1;
    
    if (!(npath = kmalloc (size))) {
        return -1;
    }
    
    memcpy (npath, path, size);
    temp = npath - 1;
    
    do {
    
        nodename = temp + 1;
        
        if ((temp = strpbrk (temp + 1, "/|\\"))) {
            *temp = 0;                  /* insert termination (splitting up string) */
        }
        
        nparent = get_node (nodename, parent);
        
        if (!nparent && temp) {
        
            IPC_ERROR err = create_node (parent, &nparent, nodename, IPC_FOLDER);
            
            if (err != IPC_SUCCESSFUL) {
                return err;
            }
        
        }
        
        if (temp) {
            parent = nparent;
        }
    
    } while (temp);
    
    return create_node (parent, node, nodename, type);

}

IPC_ERROR ipc_delete_key (const char *path) {

    ipc_node_t *node = ipc_get_node (path);
    
    if (node == 0) {
        return IPC_NOTFOUND;
    }
    
    if (!node->parent || !access_allowed (node->parent, IPC_WRITE)) {           /* Deleting requires write access to nodes parent */
        return IPC_ACCESSDENIED;
    }

    return ipc_delete_node (node);

}

IPC_ERROR ipc_delete_node (ipc_node_t *node) {

    if (!access_allowed (node, IPC_WRITE)) {
        return IPC_ACCESSDENIED;
    }
    
    if (node->type == IPC_FOLDER) {
    
        list_node_t *e;
        IPC_ERROR err = IPC_SUCCESSFUL;
        
        for (e = node->data.folder.head; e != 0; e = e->next) {
            err = ipc_delete_node (e->value);
        }
        
        if (err != IPC_SUCCESSFUL) {
            return err;
        }
    
    }
    
    kfree (node->name);
    
    if (node->parent) {
        list_remove_index (&node->parent->data.folder, list_find (&node->parent->data.folder, node));
    }
    
    if (node->type == IPC_STRING) {
        kfree (node->data.string);
    } else if (node->type == IPC_FOLDER) {
        list_destruct (&node->data.folder);
    } else if (node->type == IPC_FILE) {
        ; // TODO
    }
    
    if (node->access_table) {
        list_free (node->access_table);
    }
    
    kfree (node);
    return IPC_SUCCESSFUL;

}

ipc_node_t *ipc_get_node (const char *path) {

    ipc_node_t *parent;
    
    if (*path == '/' || *path == '|' || *path == '\\') {
        path++;
    }
    
    parent = &root;
    
    if (*path == '~') {
    
        parent = thread_get_current ()->owner->ipc_node;
        path += 2;
    
    } else if (*path == '>') {
    
        parent = thread_get_current ()->owner->ipc_working;
        path += 2;
    
    }
    
    return get_node (path, parent);

}

IPC_ERROR ipc_set_int (const char *path, const int64_t *source, IPC_TYPE type) {

    IPC_ERROR err;
    ipc_node_t *node;
    
    if (type != IPC_INT && type != IPC_UINT && type != IPC_QWORD && type != IPC_SIZE) {
        return IPC_WRONGTYPE;
    }
    
    node = ipc_get_node (path);
    
    if ((err = prepare_node_to_write (&node, path, type)) != IPC_SUCCESSFUL) {
        return err;
    }
    
    node->data.integer = *source;
    return IPC_SUCCESSFUL;

}

IPC_ERROR ipc_set_task_working_node (const char *path, int create) {

    ipc_node_t *working = ipc_get_node (path);
    
    if (!working && create) {
        return ipc_create_node (path, &thread_get_current ()->owner->ipc_working, IPC_FOLDER);
    }
    
    thread_get_current ()->owner->ipc_working = working;
    return thread_get_current ()->owner->ipc_working ? IPC_SUCCESSFUL : IPC_NOTFOUND;

}
