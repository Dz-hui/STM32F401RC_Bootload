#include "bootloader.h"
#include "core_cm4.h"

hal_bootloader_t hal_bl;

const char *FW_FILE_SD        = "1:/ZNP_ROBIN_DW.bin";
const char *FW_OLD_FILE_SD    = "1:/ZNP_ROBIN_DW.CUR";

char firmware_name_buff[FW_NAME_SIZE];
char old_name_buff[FW_NAME_SIZE];

uint32_t msp = 0;
uint32_t reset = 0;
UINT br;

uint32_t EraseCounter = 0x00, Address = 0x00;//擦除计数，擦除地址

uint8_t file_read_buff[1024];  // 用于装载读取回来的固件
uint16_t *hlfP = (uint16_t *)file_read_buff;

/* only support cortex-M */
void nvic_set_vector_table(uint32_t NVIC_VectTab, uint32_t Offset) {

    /* Check the parameters */
    assert_param(IS_NVIC_VECTTAB(NVIC_VectTab));
    assert_param(IS_NVIC_OFFSET(Offset)); 
    SCB->VTOR = NVIC_VectTab | (Offset & (uint32_t)0x1FFFFF80);
}

/* only support cortex-M */
void bl_reset_systick(void) {

    /* Disable systick */
    SysTick->CTRL &= ~ SysTick_CTRL_ENABLE_Msk;
}

void bl_erase_flash(void) {

    hal_flash_erase();
}


void bufferSet(uint8_t* pBuffer, uint8_t data, uint16_t BufferLength)
{
  uint16_t i;
  for(i = 0; i<BufferLength; i++)
  {
    pBuffer[i] = data;
  }
}

uint32_t fw_size_count = 0;

void bl_write_flash(void) {

    FIL fil;
    Address = APP_STAR_ADDR;
    
    while(1) {

        bufferSet(file_read_buff, 0xff, READ_FILE_PAGE_SIZE);

        f_read(&fil, file_read_buff, READ_FILE_PAGE_SIZE, &br);

        fw_size_count++;

        if(msp == 0 && reset == 0)
        {
            msp = *((uint32_t *)(file_read_buff));

            reset = *((uint32_t *)(file_read_buff + 4));
        }

        hlfP = (uint16_t *)file_read_buff;

        hal_flash_write(Address, hlfP, READ_FILE_PAGE_SIZE / 2 );

		Address += READ_FILE_PAGE_SIZE;

        if(br < READ_FILE_PAGE_SIZE) {

            hal_flag.bit_read_finish = 1;

            break;
        }; 
    }
    DEBUG_PRINT("Upload size:%ldk", fw_size_count);
}



uint8_t bl_open_update_file(void) {

    FIL fil;
    FRESULT fr;
    uint32_t file_size = 0;

    memset(hal_bl.fw_name_buf, 0, sizeof(hal_bl.fw_name_buf));
    memset(old_name_buff, 0, sizeof(hal_bl.fw_old_name_buf));

    strcpy(hal_bl.fw_name_buf, FW_FILE_SD);
    strcpy(hal_bl.fw_old_name_buf, FW_OLD_FILE_SD);    

    fr = f_open(&fil, hal_bl.fw_name_buf,  FA_READ|FA_WRITE);

    file_size = fil.obj.objsize;

    if(file_size > (MCU_FLASH-BL_SIZE)) return 1;

    if(fr == FR_OK) {
        hal_sd.fw_file_size = fil.obj.objsize;
        bl_erase_flash();
        hal_flag.bit_open_file = 1;
        
        return 0;
    }else {
        hal_flag.bit_open_file = 0;
        return 1;
    }
}

void bl_rename_file(void) {

    FIL fil;

    f_close(&fil);
    f_unlink(hal_bl.fw_old_name_buf);
    f_rename(hal_bl.fw_name_buf, hal_bl.fw_old_name_buf);
}


/*
 * Author:sola
 * Fix time:2022-01-22
 * Describe:
 * 1. update flash 
 * 2. set beep
 * 3. DeInit SPI, Systick
 * 4. Reset Systick 
 * 5. set MSP
 * 6. jump and reset mcu
*/
void bl_jump_to_app(uint32_t sect, uint32_t Msp, uint32_t reset_msp) {

    uint32_t base;
    uint32_t offset;

#ifdef LCD_DGUS_DWIN
    jump_to_star();
#endif

    hal_sd_deinit();

    SysTick->CTRL &= ~ SysTick_CTRL_ENABLE_Msk;

    base = (sect > NVIC_VectTab_FLASH)? NVIC_VectTab_FLASH:NVIC_VectTab_RAM; //中断向量表基地址

    offset = sect-base;	

    nvic_set_vector_table(base, offset);

    __set_MSP(Msp);

    ((void(*)()) (reset_msp))();
}


void jump_without_update(void) {

    printf_result_info();

    msp = *((uint32_t *)(APP_STAR_ADDR));

	reset = *((uint32_t *)(APP_STAR_ADDR + 4));

    bl_jump_to_app(APP_STAR_ADDR, msp, reset);

}

void jump_with_update() {

#ifdef LCD_DGUS_DWIN
    jump_into_boot_screen();
#endif

    bl_write_flash();
    bl_rename_file();
    printf_result_info();
    bl_jump_to_app(APP_STAR_ADDR, msp, reset);
}

void update_check(void) {

    uint8_t is_need_update;

    is_need_update = bl_open_update_file();

    if(is_need_update == 0) {
            
        hal_flag.bit_uploading = 1;
        jump_with_update();
    }
    else   {        
        hal_flag.bit_uploading = 0;     // open file fail or no fw file
        jump_without_update();
    }
}


