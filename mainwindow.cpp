#include "mainwindow.h"
#include "qmessagebox.h"
#include "ui_mainwindow.h"
#include <cstdio>

#include <QMessageBox>
#include <QTimer>
#include <cstring>
#include <stlink.h>
// stlink-rtt 定义

typedef struct {
  uint32_t sName;   // Optional name. Standard names so far are: "Terminal",
                    // "SysView", "J-Scope_t4i4"
  uint32_t pBuffer; // Pointer to start of buffer
  uint32_t SizeOfBuffer; // Buffer size in bytes. Note that one byte is lost, as
                         // this implementation does not fill up the buffer in
                         // order to avoid the problem of being unable to
                         // distinguish between full and empty.
  uint32_t WrOff; // Position of next item to be written by either target.
  uint32_t RdOff; // Position of next item to be read by host. Must be volatile
                  // since it may be modified by host.
  uint32_t Flags; // Contains configuration flags
} rtt_channel;

typedef struct {
  int8_t acID[16]; // Initialized to "SEGGER RTT"
  int32_t
      MaxNumUpBuffers; // Initialized to SEGGER_RTT_MAX_NUM_UP_BUFFERS (type. 2)
  int32_t MaxNumDownBuffers; // Initialized to SEGGER_RTT_MAX_NUM_DOWN_BUFFERS
                             // (type. 2)
  int32_t cb_size;
  int32_t cb_addr;
  rtt_channel *aUp;   // Up buffers, transferring information up from target via
                      // debug probe to host
  rtt_channel *aDown; // Down buffers, transferring information down from host
                      // via debug probe to target
} rtt_cb;

QTimer *timer;
//全局stlink句柄
stlink_t *sl = NULL;
rtt_cb *rtt_cb_ptr;

/**
 * @brief read_mem 从addr读取len长度的内存到数组des里
 * @param des  目标数据存放缓冲区
 * @param addr  设备的内存地址
 * @param len   要读取的长度
 * @return 成功读取返回0
 */
int read_mem(uint8_t *des, uint32_t addr, uint32_t len) {
  uint32_t offset_addr = 0, offset_len, read_len;

  // address and read len need to align to 4
  read_len = len;
  offset_addr = addr % 4;
  if (offset_addr > 0) {
    addr -= offset_addr;
    read_len += offset_addr;
  }
  offset_len = read_len % 4;
  if (offset_len > 0)
    read_len += (4 - offset_len);

  stlink_read_mem32(sl, addr, read_len);

  // read data we actually need
  for (uint32_t i = 0; i < len; i++)
    des[i] = (uint8_t)sl->q_buf[i + offset_addr];

  return 0;
}

int write_mem(uint8_t *buf, uint32_t addr, uint32_t len) {
  for (uint32_t i = 0; i < len; i++)
    sl->q_buf[i] = buf[i];

  stlink_write_mem8(sl, addr, len);

  return 0;
}

int get_channel_data(uint8_t *buf, rtt_channel *rtt_c,
                     uint32_t rtt_channel_addr) {
  uint32_t len;

  if (rtt_c->WrOff > rtt_c->RdOff) {
    len = rtt_c->WrOff - rtt_c->RdOff;
    read_mem(buf, rtt_c->pBuffer + rtt_c->RdOff, len);
    rtt_c->RdOff += len;
    write_mem((uint8_t *)&(rtt_c->RdOff), rtt_channel_addr + 4 * 4, 4);
    return 1;
  } else if (rtt_c->WrOff < rtt_c->RdOff) {
    len = rtt_c->SizeOfBuffer - rtt_c->RdOff;
    read_mem(buf, rtt_c->pBuffer + rtt_c->RdOff, len);
    read_mem(buf + len, rtt_c->pBuffer, rtt_c->WrOff);
    rtt_c->RdOff = rtt_c->WrOff;
    write_mem((uint8_t *)&(rtt_c->RdOff), rtt_channel_addr + 4 * 4, 4);
    return 1;
  } else {
    return 0;
  }
}

void MainWindow::timer_cb(void) {
  uint8_t buf[1024];
  char str_buf[1024];
  int line_cnt;

  // update SEGGER_RTT_CB content
  read_mem(buf, rtt_cb_ptr->cb_addr + 24, rtt_cb_ptr->cb_size - 24);
  for (uint32_t j = 0; j < rtt_cb_ptr->MaxNumUpBuffers * sizeof(rtt_channel);
       j++)
    ((uint8_t *)rtt_cb_ptr->aUp)[j] = buf[j];
  for (uint32_t j = 0; j < rtt_cb_ptr->MaxNumDownBuffers * sizeof(rtt_channel);
       j++)
    ((uint8_t *)rtt_cb_ptr->aDown)[j] =
        buf[rtt_cb_ptr->MaxNumUpBuffers * sizeof(rtt_channel) + j];

  memset(buf, '\0', 1024);
  if (get_channel_data(buf, &rtt_cb_ptr->aUp[0], rtt_cb_ptr->cb_addr + 24) >
      0) {
    if (strlen((const char *)buf) > 0) {
      strcpy(str_buf, (const char *)buf);
      ui->textEdit->insertPlainText(QString::fromUtf8(str_buf));

      //    line_cnt = IupGetInt(txt_message, "LINECOUNT");
      //   IupSetStrf(txt_message, "SCROLLTO", "%d:1", line_cnt);
    }
  }
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
  ui->setupUi(this);
  this->setWindowTitle("STLink-RTT-QT");
  /* 创建定时器 */
  timer = new QTimer(this);

  /* 关联槽函数 */
  connect(timer, SIGNAL(timeout()), this, SLOT(timer_cb()));
}

MainWindow::~MainWindow() {
  delete ui;
  //退出程序时释放资源
  if (sl) {
    stlink_exit_debug_mode(sl);
    stlink_close(sl);
  }
  if (rtt_cb_ptr->aUp != NULL)
    free(rtt_cb_ptr->aUp);
  if (rtt_cb_ptr->aDown != NULL)
    free(rtt_cb_ptr->aDown);
  if (rtt_cb_ptr != NULL)
    free(rtt_cb_ptr);
}

static stlink_t *stlink_open_first(void) {
  stlink_t *sl = NULL;
  sl = stlink_v1_open(0, 1);
  if (sl == NULL)
    sl = stlink_open_usb(ugly_loglevel::UINFO, CONNECT_NORMAL, NULL, 0);
  return sl;
}

void MainWindow::on_pushButton_clicked() {
  if (sl == NULL) {
    sl = stlink_open_first();

    if (sl == NULL) {
      QMessageBox::warning(NULL, "错误", "fail to open stlink",
                           QMessageBox::Yes);
      return;
    }

    sl->verbose = 1;

    if (stlink_current_mode(sl) == STLINK_DEV_DFU_MODE)
      stlink_exit_dfu_mode(sl);

    if (stlink_current_mode(sl) != STLINK_DEV_DEBUG_MODE)
      stlink_enter_swd_mode(sl);

    // read the whole RAM
    uint8_t *buf = (uint8_t *)malloc(sl->sram_size);
    uint32_t r_cnt = sl->sram_size / 0x400;
    qDebug("target have %u k ram\n\r", r_cnt);
    for (uint32_t i = 0; i < r_cnt; i++) {
      stlink_read_mem32(sl, 0x20000000 + i * 0x400, 0x400);
      for (uint32_t k = 0; k < 0x400; k++)
        (buf + i * 0x400)[k] = (uint8_t)(sl->q_buf[k]);
    }

    rtt_cb_ptr = (rtt_cb *)malloc(sizeof(rtt_cb));
    rtt_cb_ptr->cb_addr = 0;

    // find SEGGER_RTT_CB address
    uint32_t offset;
    for (offset = 0; offset < sl->sram_size - 16; offset++) {
      if (strncmp((char *)&buf[offset], "SEGGER RTT", 16) == 0) {
        rtt_cb_ptr->cb_addr = 0x20000000 + offset;
        qDebug("addr = 0x%x\n\r", rtt_cb_ptr->cb_addr);
        break;
      }
    }

    if (rtt_cb_ptr->cb_addr == 0) {
      QMessageBox::warning(NULL, "错误", "NO SEGGER_RTT_CB found!",
                           QMessageBox::Yes);
      free(buf);
      return;
    }

    // get SEGGER_RTT_CB content
    memcpy(rtt_cb_ptr->acID, ((rtt_cb *)(buf + offset))->acID, 16);
    rtt_cb_ptr->MaxNumUpBuffers = ((rtt_cb *)(buf + offset))->MaxNumUpBuffers;
    rtt_cb_ptr->MaxNumDownBuffers =
        ((rtt_cb *)(buf + offset))->MaxNumDownBuffers;
    rtt_cb_ptr->cb_size =
        24 + (rtt_cb_ptr->MaxNumUpBuffers + rtt_cb_ptr->MaxNumDownBuffers) *
                 sizeof(rtt_cb);
    rtt_cb_ptr->aUp = (rtt_channel *)malloc(rtt_cb_ptr->MaxNumUpBuffers *
                                            sizeof(rtt_channel));
    rtt_cb_ptr->aDown = (rtt_channel *)malloc(rtt_cb_ptr->MaxNumDownBuffers *
                                              sizeof(rtt_channel));
    memcpy(rtt_cb_ptr->aUp, buf + offset + 24,
           rtt_cb_ptr->MaxNumUpBuffers * sizeof(rtt_channel));
    memcpy(rtt_cb_ptr->aDown,
           buf + offset + 24 +
               rtt_cb_ptr->MaxNumUpBuffers * sizeof(rtt_channel),
           rtt_cb_ptr->MaxNumDownBuffers * sizeof(rtt_channel));

    free(buf);
    stlink_run(sl, run_type::RUN_NORMAL);

    /* 启动定时器，100ms一次 */
    timer->start(100);

    ui->pushButton->setText("断开连接");
    ui->label1->setText(QString("连接至") + QString::number(r_cnt) +
                        "K ram / " + QString::number(sl->flash_size / 1024) +
                        " K Flash 设备");
  } else {
    stlink_exit_debug_mode(sl);
    stlink_close(sl);
    sl = NULL;
    timer->stop();
    ui->pushButton->setText("连接");
    ui->label1->setText(QString("断开连接"));
    free(rtt_cb_ptr->aUp);
    free(rtt_cb_ptr->aDown);
    free(rtt_cb_ptr);
  }

  return;
}
