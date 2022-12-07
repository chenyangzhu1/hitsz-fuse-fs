#ifndef _TYPES_H_
#define _TYPES_H_

/******************************************************************************
 * SECTION: Type def
 *******************************************************************************/
typedef int boolean;
typedef uint16_t flag16;

typedef enum newfs_file_type
{
    NEWFS_REG_FILE,
    NEWFS_DIR
} NEWFS_FILE_TYPE;
/******************************************************************************
 * SECTION: Macro
 *******************************************************************************/
#define TRUE 1
#define FALSE 0
#define UINT32_BITS 32
#define UINT8_BITS 8

#define NEWFS_MAGIC_NUM 0x00001511 
#define NEWFS_SUPER_OFS 0          //超级块的偏移（字节）
#define NEWFS_ROOT_INO 0           //超级块在位图中的索引

#define NEWFS_ERROR_NONE 0
#define NEWFS_ERROR_ACCESS EACCES
#define NEWFS_ERROR_SEEK ESPIPE
#define NEWFS_ERROR_ISDIR EISDIR
#define NEWFS_ERROR_NOSPACE ENOSPC
#define NEWFS_ERROR_EXISTS EEXIST
#define NEWFS_ERROR_NOTFOUND ENOENT
#define NEWFS_ERROR_UNSUPPORTED ENXIO
#define NEWFS_ERROR_IO EIO       /* Error Input/Output */
#define NEWFS_ERROR_INVAL EINVAL /* Invalid Args */

#define NEWFS_MAX_FILE_NAME 128 //最大文件名长度
#define NEWFS_DATA_PER_FILE 4   //每个文件最多4个EXT2下的块
#define NEWFS_DEFAULT_PERM 0777

#define NEWFS_IOC_MAGIC 'S'
#define NEWFS_IOC_SEEK _IO(NEWFS_IOC_MAGIC, 0)

#define NEWFS_FLAG_BUF_DIRTY 0x1
#define NEWFS_FLAG_BUF_OCCUPY 0x2

/******************************************************************************
 * SECTION: Macro Function
 *******************************************************************************/
#define NEWFS_IO_SZ() (newfs_super.sz_io)      //设备的IO大小
#define NEWFS_BLK_SZ() (newfs_super.sz_io * 2) /* 设备的数据块大小*/
#define NEWFS_DISK_SZ() (newfs_super.sz_disk)  //设备的磁盘大小
#define NEWFS_DRIVER() (newfs_super.driver_fd)

#define NEWFS_ROUND_DOWN(value, round) ((value) % (round) == 0 ? (value) : ((value) / (round)) * (round))
//向下对齐，若round为512，value为500。则对到0

#define NEWFS_ROUND_UP(value, round) ((value) % (round) == 0 ? (value) : ((value) / (round) + 1) * (round))
//向上对齐，若round为512，value为500，则对到512.
#define NEWFS_BLKS_SZ(blks) ((blks)*NEWFS_BLK_SZ())
#define NEWFS_ASSIGN_FNAME(pnewfs_dentry, _fname) memcpy(pnewfs_dentry->fname, _fname, strlen(_fname))
#define NEWFS_INO_OFS(ino) (newfs_super.inode_offset + (ino)*NEWFS_BLK_SZ())
#define NEWFS_DATA_OFS(blocknum) (newfs_super.data_offset + (blocknum)*NEWFS_BLK_SZ())

#define NEWFS_IS_DIR(pinode) (pinode->dentry->ftype == NEWFS_DIR)
//返回输入inode指向的是否为文件夹
#define NEWFS_IS_REG(pinode) (pinode->dentry->ftype == NEWFS_REG_FILE)
//返回输入inode指向的是否为文件
/******************************************************************************
 * SECTION: FS Specific Structure - In memory structure
 *******************************************************************************/
struct newfs_dentry;
struct newfs_inode;
struct newfs_super;

struct custom_options
{
    const char *device;
    boolean show_help;
};
/*值得一提的是，
这里我采用固定分配，
因此pointer和bno会固定指向一些位置，不会变动。*/
struct newfs_inode
{                 //仿照指导书定义
    uint32_t ino; /* 在inode位图中的下标 */
    int size;     /* 文件已占用空间 */
    int dir_cnt;
    struct newfs_dentry *dentry;                 /* 指向该inode的dentry */
    struct newfs_dentry *dentrys;                /* 所有目录项 */
    uint8_t *block_pointer[NEWFS_DATA_PER_FILE]; /* 如果是 FILE，指向 4 个数据块 */
    int blocknum[NEWFS_DATA_PER_FILE];                /* 数据块在磁盘中的块号  上述数据块在磁盘中的块号 */
};


/*2.newfs_dentry目录项结构，
这里的设计仿照sfs，目录项都是以链表形式出现在内存中，
因此parent相当于prev，brother相当于next，
ino表示他指向的inode在位图中下标*/
struct newfs_dentry
{
    char fname[NEWFS_MAX_FILE_NAME];
    struct newfs_dentry *parent;  /* 父亲 Inode 的 dentry */
    struct newfs_dentry *brother; /* 下一个兄弟 Inode 的 dentry */
    uint32_t ino;                 //它指向的inode在inode位图中的下标
    struct newfs_inode *inode;    /* 指向inode */
    NEWFS_FILE_TYPE ftype;
};
/*
在设备上，超级块中仅仅保存了文件系统的位图位置，
我们需要 将位图读到内存中以便我们进行访问 ；
此外，为了方便查找根目录，我们也完全可以将根目录维护在超级块的内存表示中，
以便我们全局访问。
因此，可以简单设计超级块的内存表示如下：

*/
struct newfs_super
{
    int driver_fd;

    int sz_io;    // io大小
    int sz_disk;  //磁盘大小
    int sz_usage; //已经使用的大小

    int max_ino;
    int max_data;

    uint8_t *map_inode;   //主存中map_inode的指针
    int map_inode_blks;   // inode位图块数
    int map_inode_offset; // inode位图偏移

    uint8_t *map_data;   //指向数据位图指针
    int map_data_blks;   //数据位图块数
    int map_data_offset; //数据位图偏移

    int inode_offset; // 索引结点的偏移
    int data_offset;  // 数据块的偏移

    boolean is_mounted;

    struct newfs_dentry *root_dentry;
};

//创建新的dentry
static inline struct newfs_dentry *new_dentry(char *fname, NEWFS_FILE_TYPE ftype)
{
    //随机分配一块内容
    struct newfs_dentry *dentry = (struct newfs_dentry *)malloc(sizeof(struct newfs_dentry));
    memset(dentry, 0, sizeof(struct newfs_dentry));
    //记录名字
    NEWFS_ASSIGN_FNAME(dentry, fname);
    //初始化属性
    dentry->ftype = ftype;
    dentry->ino = -1;
    dentry->inode = NULL;
    dentry->parent = NULL;
    dentry->brother = NULL;
    return dentry;
}

/******************************************************************************
 * SECTION: FS Specific Structure - Disk structure
 *******************************************************************************/
struct newfs_super_d
{
    uint32_t magic_num; //磁盘中超级块幻数，用于确定是否需要初始化
    int sz_usage;       //已使用大小

    int map_inode_blks;   /* inode 位图占用的块数
                          在mount中初始化时会固定设置为1 */
    int map_inode_offset; /* inode 位图在磁盘上的偏移 */

    int map_data_blks;   /* data 位图占用的块数
                            在mount初识化时会固定设置为1*/
    int map_data_offset; /* data 位图在磁盘上的偏移 */

    int inode_offset; /* 索引结点的偏移 */
    int data_offset;  /* 数据块的偏移*/
};

struct newfs_inode_d
{
    uint32_t ino; /* 在inode位图中的下标 */
    int size;     /* 文件已占用空间 */
    int dir_cnt;
    NEWFS_FILE_TYPE ftype;
    int blocknum[NEWFS_DATA_PER_FILE]; /* 数据块在磁盘中的块号 */
};

struct newfs_dentry_d
{
    char fname[NEWFS_MAX_FILE_NAME];
    NEWFS_FILE_TYPE ftype;
    uint32_t ino; /* 指向的 ino 号 */
};

#endif /* _TYPES_H_ */