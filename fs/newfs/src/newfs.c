#include "../include/newfs.h"

/******************************************************************************
 * SECTION: 宏定义
 *******************************************************************************/
#define OPTION(t, p)                             \
    {                                            \
        t, offsetof(struct custom_options, p), 1 \
    }

/******************************************************************************
 * SECTION: 全局变量
 *******************************************************************************/
static const struct fuse_opt option_spec[] = {/* 用于FUSE文件系统解析参数 */
                                              OPTION("--device=%s", device),
                                              FUSE_OPT_END};
struct newfs_super newfs_super;
struct custom_options newfs_options;
/******************************************************************************
 * SECTION: FUSE操作定义
 *******************************************************************************/
static struct fuse_operations operations = {
    .init = newfs_init,         /* mount文件系统 */
    .destroy = newfs_destroy,   /* umount文件系统 */
    .mkdir = newfs_mkdir,       /* 建目录，mkdir */
    .getattr = newfs_getattr,   /* 获取文件属性，类似stat，必须完成 */
    .readdir = newfs_readdir,   /* 填充dentrys */
    .mknod = newfs_mknod,       /* 创建文件，touch相关 */
    .write = newfs_write,       /* 写入文件 */
    .read = newfs_read,         /* 读文件 */
    .utimens = newfs_utimens,   /* 修改时间，忽略，避免touch报错 */
    .truncate = newfs_truncate, /* 改变文件大小 */
    .unlink = NULL,             /* 删除文件 */
    .rmdir = NULL,              /* 删除目录， rm -r */
    .rename = NULL,             /* 重命名，mv */

    .open = NULL,
    .opendir = NULL,
    .access = newfs_access};

/******************************************************************************
 * SECTION: 辅助函数定义  仿照sfs util
 *******************************************************************************/

/**
 * @brief 获取文件名
 *
 * @param path
 * @return char*
 */
char *newfs_get_fname(const char *path)
{
    char ch = '/';
    char *q = strrchr(path, ch) + 1;
    return q;
}
/**
 * @brief 计算路径的层级
 * exm: /av/c/d/f
 * -> lvl = 4
 * @param path
 * @return int
 */
int newfs_calc_lvl(const char *path)
{
    // char* path_cpy = (char *)malloc(strlen(path));
    // strcpy(path_cpy, path);
    char *str = path;
    int lvl = 0;
    if (strcmp(path, "/") == 0)
    {
        return lvl;
    }
    while (*str != NULL)
    {
        if (*str == '/')
        {
            lvl++;
        }
        str++;
    }
    return lvl;
}
/**
 * @brief 封装ddriver的读,一次读1024B
 *
 * @param offset 要读的数据在磁盘上的偏移
 * @param out_content 读出的数据首地址放到out_content
 * @param size 要读出的数据大小(字节)
 * @return int
 */

int newfs_driver_read(int offset, uint8_t *out_content, int size)
{
    int offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLK_SZ());
    //向下取整，然后把取整失去的部分加到size，为了就是按块读取

    //读取的时候改为BLKSZ，按块读取 offset aligned是要读的数据在磁盘上的位置
    int bias = offset - offset_aligned;
    int size_aligned = NEWFS_ROUND_UP((size + bias), NEWFS_BLK_SZ());
    //读取的时候改为BLKSZ，按块读取
    //这里拿到对齐后的，需要读出的数据的大小

    //分配的内存按EXT2的数据块大小对齐
    //申请一块临时的内存，用cur指过去
    uint8_t *temp_content = (uint8_t *)malloc(size_aligned);
    uint8_t *cur = temp_content;
    // lseek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);

    //移动磁头到指定数据位置
    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        //从磁盘中读取数据到上面申请的内存空间中
        //每次读写都需要 读写完整的512B 的磁盘块，因此这里还是IOsz
        ddriver_read(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        //修改内存指针和剩下需要读取的大小，如果剩下需要读取的大小为0，就结束
        cur += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();
    }
    //读出来的东西都在申请的临时空间temp content中，直接copy到目标位置即可
    memcpy(out_content, temp_content + bias, size);

    //释放申请的空间
    free(temp_content);
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 写入数据
 *
 * @param fd ddriver设备handler
 * @param buf 要写入的数据Buf
 * @param size 要写入的数据大小
 * @return int 0成功，否则失败
 */

int newfs_driver_write(int offset, uint8_t *in_content, int size)
{
    int offset_aligned = NEWFS_ROUND_DOWN(offset, NEWFS_BLK_SZ());
    int bias = offset - offset_aligned;
    int size_aligned = NEWFS_ROUND_UP((size + bias), NEWFS_BLK_SZ());
    uint8_t *temp_content = (uint8_t *)malloc(size_aligned);
    uint8_t *cur = temp_content;
    newfs_driver_read(offset_aligned, temp_content, size_aligned);
    memcpy(temp_content + bias, in_content, size);

    // lseek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    ddriver_seek(NEWFS_DRIVER(), offset_aligned, SEEK_SET);
    while (size_aligned != 0)
    {
        ddriver_write(NEWFS_DRIVER(), cur, NEWFS_IO_SZ());
        cur += NEWFS_IO_SZ();
        size_aligned -= NEWFS_IO_SZ();
    }

    free(temp_content);
    return NEWFS_ERROR_NONE;
}
/**
 * @brief 为一个inode分配dentry，采用头插法
 *dentry加入到inode
 * @param inode
 * @param dentry
 * @return int
 */
int newfs_alloc_dentry(struct newfs_inode *inode, struct newfs_dentry *dentry)
{
    //如果inode的dentrys为空，就直接让这个head指向dentry
    if (inode->dentrys == NULL)
    {
        inode->dentrys = dentry;
    }
    else
    {
        //否则利用头插法，将dentry的next指向inode的头，再把这个头指向dentry
        dentry->brother = inode->dentrys;
        inode->dentrys = dentry;
    }
    inode->dir_cnt++;
    return inode->dir_cnt;
}
/**
 * @brief 分配一个inode，占用位图
 *
 * @param dentry 该dentry指向分配的inode
 * @return newfs_inode
 */
struct newfs_inode *newfs_alloc_inode(struct newfs_dentry *dentry)
{
    struct newfs_inode *inode;
    int byte_cursor = 0; //按byte对位图进行定位
    int bit_cursor = 0;  //定位位图中的bit
    int ino_cursor = 0;  //在inode位图中的编号，其实这里的ino cursor和bit cursor永远是一样的，这个变量删掉也沒事
    int data_blk_cnt = 0;
    boolean is_find_free_entry = FALSE;
    boolean data_blk_flag = FALSE;

    //对位图进行遍历，先按字节找再按比特找
    for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_inode_blks);
         byte_cursor++)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
        {
            //利用位运算，确定某一位是否为0，如果为0说明他对应的inode位图为空闲，就可以拿来分配。
            if ((newfs_super.map_inode[byte_cursor] & (0x1 << bit_cursor)) == 0)
            {
                //确定拿来分配以后就要把相应的位图设置为1，表示被占用
                newfs_super.map_inode[byte_cursor] |= (0x1 << bit_cursor);
                is_find_free_entry = TRUE;
                break;
            }
            ino_cursor++;
        }
        if (is_find_free_entry)
        {
            break;
        }
    }

    if (!is_find_free_entry || ino_cursor == newfs_super.max_ino)
        return -NEWFS_ERROR_NOSPACE;

    //这一块模仿sfs
    /* 先分配一个 inode */
    inode = (struct newfs_inode *)malloc(sizeof(struct newfs_inode));
    inode->ino = ino_cursor;
    inode->size = 0;

    /* dentry指向inode */
    dentry->inode = inode;
    dentry->ino = inode->ino;

    /* inode指回dentry */
    inode->dentry = dentry;

    inode->dir_cnt = 0;
    inode->dentrys = NULL;

    // inode分配完了就要去处理固定分配策略，以及数据位图
    //下面相当于是给这个inode的四个指针分配相应的空间,固定分配策略
    /*为这个inode指定四个磁盘块，因为这里还是用了指针桶，所以还是要仿照上面的逻辑进行散列查询
    如果是文件还会顺带指定随机快，也是因为是固定分配，就直接在这里把随机块给固定下来了
    */
    int block_num = 0;
    if (NEWFS_IS_REG(inode))
    {
        for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_data_blks);
             byte_cursor++)
        {
            for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
            {
                //如果这个数据位图是空闲的
                if ((newfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0)
                {
                    //取出来，标明1
                    newfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);
                    //因为是文件类型，直接分配指针和blocknum
                    inode->block_pointer[data_blk_cnt] = (uint8_t *)malloc(NEWFS_BLK_SZ());
                    inode->blocknum[data_blk_cnt++] = block_num;
                    if (data_blk_cnt == NEWFS_DATA_PER_FILE)
                    {
                        data_blk_flag = TRUE;
                        break;
                    }
                }
                block_num++;
            }

            if (data_blk_flag)
            {
                break;
            }
        }
    }
    else
    {
        for (byte_cursor = 0; byte_cursor < NEWFS_BLKS_SZ(newfs_super.map_data_blks);
             byte_cursor++)
        {
            for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++)
            {
                if ((newfs_super.map_data[byte_cursor] & (0x1 << bit_cursor)) == 0)
                {
                    newfs_super.map_data[byte_cursor] |= (0x1 << bit_cursor);
                    //不是文件类型，只需要分配磁盘块,不分配内存空间
                    inode->blocknum[data_blk_cnt++] = bit_cursor;
                    if (data_blk_cnt == NEWFS_DATA_PER_FILE)
                    {
                        data_blk_flag = TRUE;
                        break;
                    }
                }
                // blocknum_cursor++;
            }
            if (data_blk_flag)
            {
                break;
            }
        }
    }

    if (!data_blk_flag || bit_cursor == newfs_super.max_data)
        return -NEWFS_ERROR_NOSPACE;

    return inode;
}

/**
 * @brief 将内存中的inode写回相应到磁盘的inode
 *这里是仅仅写回inode本身的内容
 * @param inode
 * @return int
 */
int newfs_sync_inode_d(struct newfs_inode *inode)
{
    //建一个临时的磁盘inode，写入相关属性
    struct newfs_inode_d inode_d;
    inode_d.ino = inode->ino;
    inode_d.size = inode->size;
    inode_d.ftype = inode->dentry->ftype;
    inode_d.dir_cnt = inode->dir_cnt;
    int blk_cnt = 0;
    //数据块的块号写回，因为这个是我们新加入的
    for (blk_cnt = 0; blk_cnt < NEWFS_DATA_PER_FILE; blk_cnt++)
        inode_d.blocknum[blk_cnt] = inode->blocknum[blk_cnt]; /* 数据块的块号也要赋值 */
    //剩下的用driver write  把磁盘inode写回磁盘
    if (newfs_driver_write(NEWFS_INO_OFS(inode->ino), (uint8_t *)&inode_d,
                           sizeof(struct newfs_inode)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 将内存inode及其下方结构全部刷回磁盘
 *
 * @param inode
 * @return int
 */
int newfs_sync_inode(struct newfs_inode *inode)
{
    //新建一个磁盘类型inode，为后面写入磁盘准备
    struct newfs_inode_d inode_d;
    struct newfs_dentry *dentry_cursor;
    struct newfs_dentry_d dentry_d;
    int ino = inode->ino;
    //把相应的属性设置好
    inode_d.ino = ino;
    inode_d.size = inode->size;
    inode_d.ftype = inode->dentry->ftype;
    inode_d.dir_cnt = inode->dir_cnt;
    //先写回inode本身的
    newfs_sync_inode_d(inode);
    int blk_cnt = 0;
    int offset;
    //再写回inode下方的
    /* Cycle 1: 写 INODE */
    /* Cycle 2: 写 数据 */
    if (NEWFS_IS_DIR(inode)) //因为是目录类型，因此要写回目录项dentry
    {
        blk_cnt = 0;
        dentry_cursor = inode->dentrys;
        //拿到内存中的dentry的头，内存中的inode是我们要写回磁盘的东西

        while (dentry_cursor != NULL) //退出条件就是遍历完内存inode的所有dentry
        {
            //找到inode分配的首个数据块
            offset = NEWFS_DATA_OFS(inode->blocknum[blk_cnt]); // dentry 从 inode 分配的首个数据块开始存
            /* 写满一个 blk 时换到下一个 blocknum */
            while (dentry_cursor != NULL)
            { //把内存中的dentry，复制到上面申请的临时的磁盘dentry中
                memcpy(dentry_d.fname, dentry_cursor->fname, NEWFS_MAX_FILE_NAME);
                dentry_d.ftype = dentry_cursor->ftype;
                dentry_d.ino = dentry_cursor->ino;
                //把磁盘的dentry写回磁盘
                if (newfs_driver_write(offset, (uint8_t *)&dentry_d,
                                       sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE)
                {
                    NEWFS_DBG("[%s] io error\n", __func__);
                    return -NEWFS_ERROR_IO;
                }
                //如果这个内存dentry下还有inode，就递归把它写进磁盘
                if (dentry_cursor->inode != NULL)
                {
                    newfs_sync_inode(dentry_cursor->inode);
                }
                //处理完这个dentry，去到他的下一个dentry
                dentry_cursor = dentry_cursor->brother;
                //修改offset
                offset += sizeof(struct newfs_dentry_d);
            }
            blk_cnt++; /* 访问下一个指向的数据块 */
        }
    }
    else if (NEWFS_IS_REG(inode)) //如果是文件，就直接写回数据块
    {
        for (blk_cnt = 0; blk_cnt < NEWFS_DATA_PER_FILE; blk_cnt++)
        {
            if (newfs_driver_write(NEWFS_DATA_OFS(inode->blocknum[blk_cnt]),
                                   inode->block_pointer[blk_cnt], NEWFS_BLK_SZ()) != NEWFS_ERROR_NONE)
            {
                NEWFS_DBG("[%s] io error\n", __func__);
                return -NEWFS_ERROR_IO;
            }
        }
    }
    return NEWFS_ERROR_NONE;
}
/**
 * @brief
 *
 * @param dentry dentry指向ino，读取该inode
 * @param ino inode唯一编号
 * @return struct newfs_inode*
 */
struct newfs_inode *newfs_read_inode(struct newfs_dentry *dentry, int ino)
{
    struct newfs_inode *inode = (struct newfs_inode *)malloc(sizeof(struct newfs_inode));
    struct newfs_inode_d inode_d;
    struct newfs_dentry *sub_dentry;
    struct newfs_dentry_d dentry_d;
    int blk_cnt = 0;
    int dir_cnt = 0;

    //从第ino个inode中把磁盘中的inode读到inode_d中
    if (newfs_driver_read(NEWFS_INO_OFS(ino), (uint8_t *)&inode_d,
                          sizeof(struct newfs_inode_d)) != NEWFS_ERROR_NONE)
    {
        NEWFS_DBG("[%s] io error\n", __func__);
        return NULL;
    }
    inode->dir_cnt = 0;
    inode->ino = inode_d.ino;
    inode->size = inode_d.size;
    inode->dentry = dentry; /* 指回父级 dentry*/
    inode->dentrys = NULL;
    for (blk_cnt = 0; blk_cnt < NEWFS_DATA_PER_FILE; blk_cnt++)
        inode->blocknum[blk_cnt] = inode_d.blocknum[blk_cnt];
    //在内存中重建ino对应的inode，因为他和磁盘中的inode_d结构不同

    if (NEWFS_IS_DIR(inode)) //如果是文件夹，则读入目录
    {
        dir_cnt = inode_d.dir_cnt;
        //这里我发现了一个神奇的事情，就即使连续读也是可以读进来，说明虽然设计了离散的指针桶
        //但其实没起作用，他们还是连续的
        //一共有dir_cnt个dentry要读
        for (int i = 0; i < dir_cnt; i++)
        { //从磁盘中依次读进来
            if (newfs_driver_read(NEWFS_INO_OFS(ino) + i * sizeof(struct newfs_dentry_d), (uint8_t *)&dentry_d,
                                  sizeof(struct newfs_dentry_d)) != NEWFS_ERROR_NONE)
            {
                NEWFS_DBG("[%s] io error\n", __func__);
                return NULL;
            }
            //用subdentry来重建，并分配给inode
            sub_dentry = new_dentry(dentry_d.fname, dentry_d.ftype);
            sub_dentry->parent = inode->dentry;
            sub_dentry->ino = dentry_d.ino;
            newfs_alloc_dentry(inode, sub_dentry);
        }
    }
    else if (NEWFS_IS_REG(inode)) //如果是文件，则读入数据块
    {
        for (blk_cnt = 0; blk_cnt < NEWFS_DATA_PER_FILE; blk_cnt++)
        {
            inode->block_pointer[blk_cnt] = (uint8_t *)malloc(NEWFS_BLK_SZ()); /* 只分配一个块 */
            if (newfs_driver_read(NEWFS_DATA_OFS(inode->blocknum[blk_cnt]), inode->block_pointer[blk_cnt],
                                  NEWFS_BLK_SZ()) != NEWFS_ERROR_NONE)
            {
                NEWFS_DBG("[%s] io error\n", __func__);
                return NULL;
            }
        }
    }
    return inode;
}
/**
 * @brief 寻找inode下的第dir个目录项
 *
 * @param path
 * @return struct newfs_inode*
 */

struct newfs_dentry *newfs_get_dentry(struct newfs_inode *inode, int dir)
{
    struct newfs_dentry *dentry_cursor = inode->dentrys;
    int cnt = 0;
    while (dentry_cursor)
    {
        if (dir == cnt)
        {
            return dentry_cursor;
        }
        cnt++;
        dentry_cursor = dentry_cursor->brother;
    }
    return NULL;
}
/**
 * @brief
 * path: /qwe/ad  total_lvl = 2,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 *      3) find qwe's inode     lvl = 2
 *      4) find ad's dentry
 *
 * path: /qwe     total_lvl = 1,
 *      1) find /'s inode       lvl = 1
 *      2) find qwe's dentry
 *它的作用是找到路径所对应的目录项，或者返回上一级目录项
 路径解析
 * @param path
 * @return struct newfs_inode*
 */
struct newfs_dentry *newfs_lookup(const char *path, boolean *is_find, boolean *is_root)
{
    struct newfs_dentry *dentry_cursor = newfs_super.root_dentry;
    struct newfs_dentry *dentry_ret = NULL;
    struct newfs_inode *inode;
    int total_lvl = newfs_calc_lvl(path);
    int lvl = 0;
    boolean is_hit;
    char *fname = NULL;
    char *path_cpy = (char *)malloc(sizeof(path));
    *is_root = FALSE;
    strcpy(path_cpy, path);
    //首先计算路径的级数，如果为0说明是根目录。
    if (total_lvl == 0)
    { /* 根目录 */
        *is_find = TRUE;
        *is_root = TRUE;
        dentry_ret = newfs_super.root_dentry;
    }

    //不为0则需要从根目录开始，依次匹配路径中的目录项，直到找到文件所对应的目录项。
    //如果没找到则返回最后一次匹配的目录项。
    fname = strtok(path_cpy, "/");
    while (fname)
    {
        lvl++;
        if (dentry_cursor->inode == NULL) // inode未被读入
        {                                 /* Cache机制 */
                                          //读进来即可
            newfs_read_inode(dentry_cursor, dentry_cursor->ino);
        }

        inode = dentry_cursor->inode;

        if (NEWFS_IS_REG(inode) && lvl < total_lvl)
        { //该目录项的inode为文件，则返回上一级目录
            NEWFS_DBG("[%s] not a dir\n", __func__);
            dentry_ret = inode->dentry; //上一级dentry
            break;
        }
        if (NEWFS_IS_DIR(inode))
        { //如果是目录，进入该目录项
            dentry_cursor = inode->dentrys;
            is_hit = FALSE;
            //找到当前inode下文件名字与fname相同的目录项
            while (dentry_cursor)
            { //判断名称是否相同
                if (memcmp(dentry_cursor->fname, fname, strlen(fname)) == 0)
                {
                    is_hit = TRUE;
                    break;
                }
                //沿着链表遍历
                dentry_cursor = dentry_cursor->brother; /* 遍历目录下的子文件 */
            }
            //当前文件夹下已经没任何文件（文件夹）名称和fname相同，则返回上一级dentry
            //并返回not found
            if (!is_hit)
            {
                *is_find = FALSE;
                NEWFS_DBG("[%s] not found %s\n", __func__, fname);
                dentry_ret = inode->dentry;
                break;
            }
            //如果上面找到了就正常返回
            if (is_hit && lvl == total_lvl)
            {
                *is_find = TRUE;
                dentry_ret = dentry_cursor;
                break;
            }
        }
        //若找到了fname，当深度还不够，则以dentry_cursor继续循环。
        fname = strtok(NULL, "/"); //获取分解的下一位
    }
    //若要返回的目录项的inode还没读入，则需先读入。
    if (dentry_ret->inode == NULL)
    {
        dentry_ret->inode = newfs_read_inode(dentry_ret, dentry_ret->ino);
    }

    return dentry_ret;
}
/**
 * @brief 挂载newfs, Layout 如下
 *
 * Layout
 * | Super | Inode Map | Data Map | Data |
 *
 *  BLK_SZ = 2 * IO_SZ
 *
 * 每个Inode占用一个Blk
 * @param options
 * @return int
 */
int newfs_mount(struct custom_options options)
{
    int ret = NEWFS_ERROR_NONE;
    int driver_fd;
    struct newfs_super_d newfs_super_d; /* 临时存放 driver 读出的超级块 */
    struct newfs_dentry *root_dentry;
    struct newfs_inode *root_inode;

    int super_blks;

    int inode_num;
    int data_num;
    int map_inode_blks;
    int map_data_blks;

    boolean is_init = FALSE; //是否被初始化

    newfs_super.is_mounted = FALSE;

    driver_fd = ddriver_open(options.device);

    if (driver_fd < 0)
    {
        return driver_fd;
    }

    newfs_super.driver_fd = driver_fd;                                        //把打开设备的句柄给到内存结构超级块
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_SIZE, &newfs_super.sz_disk); //表明设备大小和io大小
    ddriver_ioctl(NEWFS_DRIVER(), IOC_REQ_DEVICE_IO_SZ, &newfs_super.sz_io);

    //创建根目录项
    root_dentry = new_dentry("/", NEWFS_DIR);
    //对 ddriver 的访问代码
    //从磁盘中把超级块读出，到内存中，但是由于磁盘超级块与内存超级块有区别
    //需要重建内存超级块，这里是先用一个临时变量把磁盘超级快给存下来了
    if (newfs_driver_read(NEWFS_SUPER_OFS, (uint8_t *)(&newfs_super_d),
                          sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }
    /* 读取super */
    if (newfs_super_d.magic_num != NEWFS_MAGIC_NUM)
    { /* 幻数无，重建整个磁盘 */
        /* 规定各部分大小 */

        //一个超级块
        super_blks = 1;
        /*
        本实验模拟的是一个4MB大小的磁盘
        每个数据块是1KB
        考虑到磁盘内还需要存放超级块，索引位图，数据位图
        实际的数据块不会占满4MB
        故考虑向下对齐到2048个数据快
        */
        data_num = 2048;
        /*
        定义好数据块就考虑指向这些数据块的索引个数
        一个inode可以有6个指针，同样考虑对齐到4个
        因此需要512个inode
        */
        inode_num = 512;

        /*安排好了数据块和inode
        考虑他们的位图
        位图是一个bit对应一个有效
        一个数据块大小为1KB
        有8k bit
        足够
        */
        map_inode_blks = 1;
        map_data_blks = 1;

        /* 布局layout */
        newfs_super.max_ino = inode_num;
        newfs_super.max_data = data_num;

        // layout应该是
        //超级块  inode位图  数据位图  inode  datablock
        newfs_super_d.magic_num = NEWFS_MAGIC_NUM;
        newfs_super_d.map_inode_offset = NEWFS_SUPER_OFS + NEWFS_BLKS_SZ(super_blks);
        //数据位图在inode位图之后
        newfs_super_d.map_data_offset = newfs_super_d.map_inode_offset + NEWFS_BLKS_SZ(map_inode_blks);

        newfs_super_d.inode_offset = newfs_super_d.map_data_offset + NEWFS_BLKS_SZ(map_data_blks);
        newfs_super_d.data_offset = newfs_super_d.inode_offset + NEWFS_BLKS_SZ(inode_num);

        newfs_super_d.map_inode_blks = map_inode_blks;
        newfs_super_d.map_data_blks = map_data_blks;

        newfs_super_d.sz_usage = 0;

        is_init = TRUE;
    }

    //建立内存中的超级块，利用磁盘超级块建立内存超级块
    newfs_super.sz_usage = newfs_super_d.sz_usage; /* 建立 in-memory 结构 */
                                                   //索引节点位图分配空间
    newfs_super.map_inode = (uint8_t *)malloc(NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks));
    newfs_super.map_inode_blks = newfs_super_d.map_inode_blks;
    newfs_super.map_inode_offset = newfs_super_d.map_inode_offset;
    //数据块位图分配空间
    newfs_super.map_data = (uint8_t *)malloc(NEWFS_BLKS_SZ(newfs_super_d.map_data_blks));
    newfs_super.map_data_blks = newfs_super_d.map_data_blks;
    newfs_super.map_data_offset = newfs_super_d.map_data_offset;
    //索引节点和数据块在磁盘中的偏移
    newfs_super.inode_offset = newfs_super_d.inode_offset;
    newfs_super.data_offset = newfs_super_d.data_offset;

    /* 读取两个位图到内存空间 */
    if (newfs_driver_read(newfs_super_d.map_inode_offset, (uint8_t *)(newfs_super.map_inode),
                          NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }
    if (newfs_driver_read(newfs_super_d.map_data_offset, (uint8_t *)(newfs_super.map_data),
                          NEWFS_BLKS_SZ(newfs_super_d.map_data_blks)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }
    //分配根目录项
    if (is_init)
    { //给根目录项分配inode，这里是初始化inode
        root_inode = newfs_alloc_inode(root_dentry);
        newfs_sync_inode(root_inode); /* 将重建后的 根inode 写回磁盘 */
    }

    root_inode = newfs_read_inode(root_dentry, NEWFS_ROOT_INO);
    root_dentry->inode = root_inode;
    newfs_super.root_dentry = root_dentry;
    newfs_super.is_mounted = TRUE;

    return ret;
}
/**
 * @brief
 *
 * @return int
 */
int newfs_umount()
{
    //新建一个临时的磁盘超级块
    struct newfs_super_d newfs_super_d;

    if (!newfs_super.is_mounted)
    {
        return NEWFS_ERROR_NONE;
    }

    newfs_sync_inode(newfs_super.root_dentry->inode); //从根节点开始同步之后的数据
    //将主存中超级块数据同步到磁盘的超级块
    newfs_super_d.magic_num = NEWFS_MAGIC_NUM;
    newfs_super_d.sz_usage = newfs_super.sz_usage;

    newfs_super_d.map_inode_blks = newfs_super.map_inode_blks;
    newfs_super_d.map_inode_offset = newfs_super.map_inode_offset;
    newfs_super_d.map_data_blks = newfs_super.map_data_blks;
    newfs_super_d.map_data_offset = newfs_super.map_data_offset;

    newfs_super_d.inode_offset = newfs_super.inode_offset;
    newfs_super_d.data_offset = newfs_super.data_offset;
    //写回超级块
    if (newfs_driver_write(NEWFS_SUPER_OFS, (uint8_t *)&newfs_super_d,
                           sizeof(struct newfs_super_d)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }
    //写回超级块的索引块位图
    if (newfs_driver_write(newfs_super_d.map_inode_offset, (uint8_t *)(newfs_super.map_inode),
                           NEWFS_BLKS_SZ(newfs_super_d.map_inode_blks)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }
    //写回超级块的数据位图

    if (newfs_driver_write(newfs_super_d.map_data_offset, (uint8_t *)(newfs_super.map_data),
                           NEWFS_BLKS_SZ(newfs_super_d.map_data_blks)) != NEWFS_ERROR_NONE)
    {
        return -NEWFS_ERROR_IO;
    }

    free(newfs_super.map_inode);
    free(newfs_super.map_data);
    ddriver_close(NEWFS_DRIVER());

    return NEWFS_ERROR_NONE;
}

/******************************************************************************
 * SECTION: 必做函数实现
 *******************************************************************************/
/**
 * @brief 挂载（mount）文件系统
 *
 * @param conn_info 可忽略，一些建立连接相关的信息
 * @return void*
 */
void *newfs_init(struct fuse_conn_info *conn_info)
{
    /* TODO: 在这里进行挂载 */
    if (newfs_mount(newfs_options) != NEWFS_ERROR_NONE)
    {
        NEWFS_DBG("[%s] mount error\n", __func__);
        fuse_exit(fuse_get_context()->fuse);
        return NULL;
    }
    return NULL;
}

/**
 * @brief 卸载（umount）文件系统
 *
 * @param p 可忽略
 * @return void
 */
void newfs_destroy(void *p)
{
    /* TODO: 在这里进行卸载 */
    if (newfs_umount() != NEWFS_ERROR_NONE)
    {
        NEWFS_DBG("[%s] unmount error\n", __func__);
        fuse_exit(fuse_get_context()->fuse);
        return;
    }
    return;
}

/**
 * @brief 创建目录
 * ​ ①寻找上级目录项。

    ​ ②创建目录并建立连接。
 *
 * @param path 相对于挂载点的路径
 * @param mode 创建模式（只读？只写？），可忽略
 * @return int 0成功，否则失败
 */
int newfs_mkdir(const char *path, mode_t mode)
{
    (void)mode;
    boolean is_find, is_root;
    char *fname;
    //先用lookup找到上级目录项（最近目录项
    struct newfs_dentry *last_dentry = newfs_lookup(path, &is_find, &is_root);
    struct newfs_dentry *dentry;
    struct newfs_inode *inode;
    //找到了该目录，则出现错误(重复创建)
    if (is_find)
    {
        return -NEWFS_ERROR_EXISTS;
    }

    if (NEWFS_IS_REG(last_dentry->inode))
    {
        return -NEWFS_ERROR_UNSUPPORTED;
    }

    fname = newfs_get_fname(path); //获得名字
    //创建dentry并插入上级目录项 lastdentry
    dentry = new_dentry(fname, NEWFS_DIR);
    //把dentry插到inode中
    //这一块处理前驱后继的方式与mknod类似
    dentry->parent = last_dentry;
    inode = newfs_alloc_inode(dentry);
    newfs_alloc_dentry(last_dentry->inode, dentry);

    return NEWFS_ERROR_NONE;
}

/**
 * @brief 获取文件或目录的属性，该函数非常重要
 *
 * @param path 相对于挂载点的路径
 * @param newfs_stat 返回状态
 * @return int 0成功，否则失败
 */
int newfs_getattr(const char *path, struct stat *newfs_stat)
{
    boolean is_find, is_root; //标记是否找到，是否为根目录
    struct newfs_dentry *dentry = newfs_lookup(path, &is_find, &is_root);
    if (is_find == FALSE)
    {
        return -NEWFS_ERROR_NOTFOUND;
    }
    //如果是目录，修改相应状态
    //这里是要返回newfs_stat
    if (NEWFS_IS_DIR(dentry->inode))
    {
        newfs_stat->st_mode = S_IFDIR | NEWFS_DEFAULT_PERM;
        newfs_stat->st_size = dentry->inode->dir_cnt * sizeof(struct newfs_dentry_d);
    }

    //如果是文件，则相应参数的设置
    else if (NEWFS_IS_REG(dentry->inode))
    {
        newfs_stat->st_mode = S_IFREG | NEWFS_DEFAULT_PERM;
        newfs_stat->st_size = dentry->inode->size;
    }

    newfs_stat->st_nlink = 1;
    newfs_stat->st_uid = getuid();
    newfs_stat->st_gid = getgid();
    newfs_stat->st_atime = time(NULL);
    newfs_stat->st_mtime = time(NULL);
    newfs_stat->st_blksize = NEWFS_BLK_SZ(); /* 这里修改为BLKsz 因为ext2的blksz是iosz的两倍 */
    //如果是根目录就进一步修改
    if (is_root)
    {
        newfs_stat->st_size = newfs_super.sz_usage;
        newfs_stat->st_blocks = NEWFS_DISK_SZ() / NEWFS_BLK_SZ(); /* 这里修改为BLKsz 因为ext2的blksz是iosz的两倍 */
        newfs_stat->st_nlink = 2;                                 /* !特殊，根目录link数为2 */
    }
    return NEWFS_ERROR_NONE;
}

/**
 * @brief 遍历目录项，填充至buf，并交给FUSE输出
 *
 * @param path 相对于挂载点的路径
 * @param buf 输出buffer
 * @param filler 参数讲解:
 *
 * typedef int (*fuse_fill_dir_t) (void *buf, const char *name,
 *				const struct stat *stbuf, off_t off)
 * buf: name会被复制到buf中
 * name: dentry名字
 * stbuf: 文件状态，可忽略
 * off: 下一次offset从哪里开始，这里可以理解为第几个dentry
 *
 * @param offset 第几个目录项？
 * @param fi 可忽略
 * @return int 0成功，否则失败
 */
int newfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                  struct fuse_file_info *fi)
{
    /* TODO: 解析路径，获取目录的Inode，并读取目录项，利用filler填充到buf，可参考/fs/simplefs/sfs.c的sfs_readdir()函数实现 */

    // readdir 在ls的过程中每次 仅会返回一个目录项 ，其中offset参数记录着当前应该返回的目录项
    
    boolean is_find, is_root;
    int cur_dir = offset;
    //解析路径 获取dentry
    struct newfs_dentry *dentry = newfs_lookup(path, &is_find, &is_root);
    struct newfs_dentry *sub_dentry;
    struct newfs_inode *inode;
    if (is_find) //如果找到dentry
    {
        //获取inode
        inode = dentry->inode;
        //沿着inode走offset次，走到对应的dentry位置，返回给subdentry
        sub_dentry = newfs_get_dentry(inode, cur_dir);
        if (sub_dentry)
        {
            filler(buf, sub_dentry->fname, NULL, ++offset);
            //在上述代码中，我们调用filler(buf, fname, NULL, ++offset)表示
            //将fname放入buf中，并使目录项偏移加一，代表下一次访问下一个目录项。
        }
        return NEWFS_ERROR_NONE;
    }
    return -NEWFS_ERROR_NOTFOUND;
}

/**
 * @brief 创建文件
 *
 * @param path 相对于挂载点的路径
 * @param mode 创建文件的模式，可忽略
 * @param dev 设备类型，可忽略
 * @return int 0成功，否则失败
 */
int newfs_mknod(const char *path, mode_t mode, dev_t dev)
{
    boolean is_find, is_root;
    //找到创建文件路径中所对应的目录项
    struct newfs_dentry *last_dentry = newfs_lookup(path, &is_find, &is_root);
    struct newfs_dentry *dentry;
    struct newfs_inode *inode;
    char *fname;
    //如果文件存在则返回错误
    if (is_find == TRUE)
    {
        return -NEWFS_ERROR_EXISTS;
    }
    //文件不存在则在创建目录项和对应的inode，并和父目录项建立连接。
    fname = newfs_get_fname(path);

    if (S_ISREG(mode))
    {
        dentry = new_dentry(fname, NEWFS_REG_FILE);
    }
    else if (S_ISDIR(mode))
    {
        dentry = new_dentry(fname, NEWFS_DIR);
    }
    //处理前驱后继关系
    dentry->parent = last_dentry;
    inode = newfs_alloc_inode(dentry);
    newfs_alloc_dentry(last_dentry->inode, dentry);

    return NEWFS_ERROR_NONE;
}

/**
 * @brief 修改时间，为了不让touch报错
 *
 * @param path 相对于挂载点的路径
 * @param tv 实践
 * @return int 0成功，否则失败
 */
int newfs_utimens(const char *path, const struct timespec tv[2])
{
    (void)path;
    return NEWFS_ERROR_NONE;
}
/******************************************************************************
 * SECTION: 选做函数实现
 *******************************************************************************/
/**
 * @brief 写入文件
 *
 * @param path 相对于挂载点的路径
 * @param buf 写入的内容
 * @param size 写入的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 写入大小
 */
int newfs_write(const char *path, const char *buf, size_t size, off_t offset,
                struct fuse_file_info *fi)
{
    /* 选做 */
    boolean is_find, is_root;
    //找到路径对应的dentry
    struct newfs_dentry *dentry = newfs_lookup(path, &is_find, &is_root);
    struct newfs_inode *inode;
    //如果没找到就报错
    if (is_find == FALSE)
    {
        return -NEWFS_ERROR_NOTFOUND;
    }
    //获得dentry的inode
    inode = dentry->inode;
    //不是文件类型也报错
    if (NEWFS_IS_DIR(inode))
    {
        return -NEWFS_ERROR_ISDIR;
    }
    //如果偏移过了也报错
    if (inode->size < offset)
    {
        return -NEWFS_ERROR_SEEK;
    }
    //把offset 和 size换算成对应块号和偏移
    uint64_t start_blk = 0;
    uint64_t start_offset = 0;
    uint64_t end_blk = 0;
    uint64_t end_offset = 0;
    //开始块号和偏移
    start_blk = offset / NEWFS_BLK_SZ();
    start_offset = offset % NEWFS_BLK_SZ();
    //结束块号和偏移
    end_blk = (offset + size) / NEWFS_BLK_SZ();
    end_offset = (offset + size) % NEWFS_BLK_SZ();
    char *buf_offset = buf;
    //接下来就按块来操作他们
    //如果开始和结束在同一个块中，直接copy到对应的数据块即可
    if (start_blk == end_blk)
    {
        memcpy(inode->block_pointer[start_blk] + start_offset, buf, size);
    }
    else
    {
        //否则就先把第一个块的内容拷贝过去，不一定是整个块，使用好offset
        memcpy(inode->block_pointer[start_blk] + start_offset, buf,
               NEWFS_BLK_SZ() - start_offset);
        buf_offset = buf_offset + (NEWFS_BLK_SZ() - start_offset);
        start_blk++;
        //然后就处理中间的整块
        while (start_blk < end_blk && start_blk < 4)
        {
            memcpy(inode->block_pointer[start_blk], buf_offset, NEWFS_BLK_SZ());
            start_blk++;
            buf_offset = buf_offset + NEWFS_BLK_SZ();
        }
        //最后处理最后一块的部分，同样不一定是整块
        if (start_blk < 4 && start_blk == end_blk)
        {
            memcpy(inode->block_pointer[end_blk], buf_offset,
                   end_offset);
        }
    }

    inode->size = offset + size > inode->size ? offset + size : inode->size;

    return size;
}

/**
 * @brief 读取文件
 *
 * @param path 相对于挂载点的路径
 * @param buf 读取的内容
 * @param size 读取的字节数
 * @param offset 相对文件的偏移
 * @param fi 可忽略
 * @return int 读取大小
 */
int newfs_read(const char *path, char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi)
{
    /* 选做 */
    boolean is_find, is_root;
    char *buf_offset = buf;
    int start_blk = 0;
    int start_offset = 0;
    int end_blk = 0;
    int end_offset = 0;

    struct newfs_dentry *dentry = newfs_lookup(path, &is_find, &is_root);
    struct newfs_inode *inode;

    if (is_find == FALSE)
    {
        return -NEWFS_ERROR_NOTFOUND;
    }

    inode = dentry->inode;

    if (NEWFS_IS_DIR(inode))
    {
        return -NEWFS_ERROR_ISDIR;
    }

    if (inode->size < offset)
    {
        return -NEWFS_ERROR_SEEK;
    }

    start_blk = offset / NEWFS_BLK_SZ();
    start_offset = offset % NEWFS_BLK_SZ();

    end_blk = (offset + size) / NEWFS_BLK_SZ();
    end_offset = (offset + size) % NEWFS_BLK_SZ();

    if (start_blk == end_blk)
    {
        memcpy(buf, inode->block_pointer[start_blk] + start_offset, size);
    }
    else
    {

        memcpy(buf, inode->block_pointer[start_blk] + start_offset,
               NEWFS_BLK_SZ() - start_offset);
        buf_offset = buf + (NEWFS_BLK_SZ() - start_offset);
        start_blk++;

        while (start_blk < end_blk && start_blk < 4)
        {
            memcpy(buf_offset, inode->block_pointer[start_blk], NEWFS_BLK_SZ());
            start_blk++;
            buf_offset = buf_offset + NEWFS_BLK_SZ();
        }

        if (start_blk < 4 && start_blk == end_blk)
        {
            memcpy(buf_offset, inode->block_pointer[end_blk],
                   end_offset);
        }
    }

    return size;
}

/**
 * @brief 删除文件
 *
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则失败
 */
int newfs_unlink(const char *path)
{
    /* 选做 */
    return 0;
}

/**
 * @brief 删除目录
 *
 * 一个可能的删除目录操作如下：
 * rm ./tests/mnt/j/ -r
 *  1) Step 1. rm ./tests/mnt/j/j
 *  2) Step 2. rm ./tests/mnt/j
 * 即，先删除最深层的文件，再删除目录文件本身
 *
 * @param path 相对于挂载点的路径
 * @return int 0成功，否则失败
 */
int newfs_rmdir(const char *path)
{
    /* 选做 */
    return 0;
}

/**
 * @brief 重命名文件
 *
 * @param from 源文件路径
 * @param to 目标文件路径
 * @return int 0成功，否则失败
 */
int newfs_rename(const char *from, const char *to)
{
    /* 选做 */
    return 0;
}

/**
 * @brief 打开文件，可以在这里维护fi的信息，例如，fi->fh可以理解为一个64位指针，可以把自己想保存的数据结构
 * 保存在fh中
 *
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则失败
 */
int newfs_open(const char *path, struct fuse_file_info *fi)
{
    /* 选做 */
    return 0;
}

/**
 * @brief 打开目录文件
 *
 * @param path 相对于挂载点的路径
 * @param fi 文件信息
 * @return int 0成功，否则失败
 */
int newfs_opendir(const char *path, struct fuse_file_info *fi)
{
    /* 选做 */
    return 0;
}

/**
 * @brief 改变文件大小
 *
 * @param path 相对于挂载点的路径
 * @param offset 改变后文件大小
 * @return int 0成功，否则失败
 */
int newfs_truncate(const char *path, off_t offset)
{
    /* 选做 */
    boolean is_find, is_root;
    struct newfs_dentry *dentry = newfs_lookup(path, &is_find, &is_root);
    struct newfs_inode *inode;

    if (is_find == FALSE)
    {
        return -NEWFS_ERROR_NOTFOUND;
    }

    inode = dentry->inode;

    if (NEWFS_IS_DIR(inode))
    {
        return -NEWFS_ERROR_ISDIR;
    }

    inode->size = offset;

    return NEWFS_ERROR_NONE;
    return 0;
}

/**
 * @brief 访问文件，因为读写文件时需要查看权限
 *
 * @param path 相对于挂载点的路径
 * @param type 访问类别
 * R_OK: Test for read permission.
 * W_OK: Test for write permission.
 * X_OK: Test for execute permission.
 * F_OK: Test for existence.
 *
 * @return int 0成功，否则失败
 */
int newfs_access(const char *path, int type)
{
    /* 选做: 解析路径，判断是否存在 */
    boolean is_find, is_root;
    boolean is_access_ok = FALSE;
    struct newfs_dentry *dentry = newfs_lookup(path, &is_find, &is_root);
    struct newfs_inode *inode;

    switch (type)
    {
    case R_OK:
        is_access_ok = TRUE;
        break;
    case F_OK:
        if (is_find)
        {
            is_access_ok = TRUE;
        }
        break;
    case W_OK:
        is_access_ok = TRUE;
        break;
    case X_OK:
        is_access_ok = TRUE;
        break;
    default:
        break;
    }
    return is_access_ok ? NEWFS_ERROR_NONE : -NEWFS_ERROR_ACCESS;
}
/******************************************************************************
 * SECTION: FUSE入口
 *******************************************************************************/
int main(int argc, char **argv)
{
    int ret;
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    newfs_options.device = strdup("/home/students/200111511/ddriver");

    if (fuse_opt_parse(&args, &newfs_options, option_spec, NULL) == -1)
        return -1;

    //从这里读取指令进行执行
    ret = fuse_main(args.argc, args.argv, &operations, NULL);
    fuse_opt_free_args(&args);
    return ret;
}