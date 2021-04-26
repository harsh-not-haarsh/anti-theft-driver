/***************************************************************************//**
*  \file       driver.c
*
*  \details    Simple Anit-Theft-Driver using GPIO Interrupt
*
*  \author     Dhruv Patel Harsh Anand Yashvi Ramanuj
*
*  \Tested with Linux XX raspberrypi XX
*
*******************************************************************************/
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/uaccess.h>  //copy_to/from_user()
#include <linux/gpio.h>     //GPIO
#include <linux/interrupt.h>

/* Since debounce is not supported in Raspberry pi, I have addded this to disable 
** the false detection (multiple IRQ trigger for one interrupt).
** Many other hardware supports GPIO debounce, I don't want care about this even 
** if this has any overhead. Our intention is to explain the GPIO interrupt.
** If you want to disable this extra coding, you can comment the below macro.
** This has been taken from : https://raspberrypi.stackexchange.com/questions/8544/gpio-interrupt-debounce
**
** If you want to use Hardaware Debounce, then comment this EN_DEBOUNCE.
**
*/
#define EN_DEBOUNCE

#ifdef EN_DEBOUNCE
#include <linux/jiffies.h>

extern unsigned long volatile jiffies;
unsigned long old_jiffie = 0;
#endif

//LED is connected to this GPIO
#define GPIO_21_OUT (21)

//IR Sensor is connected to this GPIO
#define GPIO_16_IN  (16)

//GPIO_16_IN value toggle
unsigned int led_toggle = 0; 

//This used for storing the IRQ number for the GPIO
unsigned int GPIO_irqNumber;

//Interrupt handler for GPIO 16. This will be called whenever there is a raising edge detected. 
static irqreturn_t gpio_irq_handler(int irq,void *dev_id) 
{
  static unsigned long flags = 0;
  
#ifdef EN_DEBOUNCE
   unsigned long diff = jiffies - old_jiffie;
   if (diff < 20)
   {
     return IRQ_HANDLED;
   }
  
  old_jiffie = jiffies;
#endif  

  local_irq_save(flags);
  led_toggle = (0x01 ^ led_toggle);                             // toggle the old value
  gpio_set_value(GPIO_21_OUT, led_toggle);                      // toggle the GPIO_21_OUT
  pr_info("Interrupt Occurred : GPIO_21_OUT : %d ",gpio_get_value(GPIO_21_OUT));
  local_irq_restore(flags);
  return IRQ_HANDLED;
}
 
dev_t dev = 0;
static struct class *dev_class;
static struct cdev atd_cdev;
 
static int __init atd_driver_init(void);
static void __exit atd_driver_exit(void);
 
 
/*************** Driver functions **********************/
static int atd_open(struct inode *inode, struct file *file);
static int atd_release(struct inode *inode, struct file *file);
static ssize_t atd_read(struct file *filp, 
                char __user *buf, size_t len,loff_t * off);
static ssize_t atd_write(struct file *filp, 
                const char *buf, size_t len, loff_t * off);
/******************************************************/

//File operation structure 
static struct file_operations fops =
{
  .owner          = THIS_MODULE,
  .read           = atd_read,
  .write          = atd_write,
  .open           = atd_open,
  .release        = atd_release,
};


/*
** This function will be called when we open the Device file
*/ 
static int atd_open(struct inode *inode, struct file *file)
{
  pr_info("Device File Opened...!!!\n");
  return 0;
}

/*
** This function will be called when we close the Device file
*/ 
static int atd_release(struct inode *inode, struct file *file)
{
  pr_info("Device File Closed...!!!\n");
  return 0;
}

/*
** This function will be called when we read the Device file
*/ 
static ssize_t atd_read(struct file *filp, 
                char __user *buf, size_t len, loff_t *off)
{
  uint8_t gpio_state = 0;
  
  //reading GPIO value
  gpio_state = gpio_get_value(GPIO_21_OUT);
  
  //write to user
  len = 1;
  if( copy_to_user(buf, &gpio_state, len) > 0) {
    pr_err("ERROR: Not all the bytes have been copied to user\n");
  }
  
  pr_info("Read function : GPIO_21 = %d \n", gpio_state);
  
  return 0;
}

/*
** This function will be called when we write the Device file
*/
static ssize_t atd_write(struct file *filp, 
                const char __user *buf, size_t len, loff_t *off)
{
  uint8_t rec_buf[10] = {0};
  
  if( copy_from_user( rec_buf, buf, len ) > 0) {
    pr_err("ERROR: Not all the bytes have been copied from user\n");
  }
  
  pr_info("Write Function : GPIO_21 Set = %c\n", rec_buf[0]);
  
  if (rec_buf[0]=='1') {
    //set the GPIO value to HIGH
    gpio_set_value(GPIO_21_OUT, 1);
  } else if (rec_buf[0]=='0') {
    //set the GPIO value to LOW
    gpio_set_value(GPIO_21_OUT, 0);
  } else {
    pr_err("Unknown command : Please provide either 1 or 0 \n");
  }
  
  return len;
}

/*
** Module Init function
*/ 
static int __init atd_driver_init(void)
{
  /*Allocating Major number*/
  if((alloc_chrdev_region(&dev, 0, 1, "atd_Dev")) <0){
    pr_err("Cannot allocate major number\n");
    goto r_unreg;
  }
  pr_info("Major = %d Minor = %d \n",MAJOR(dev), MINOR(dev));

  /* Creating cdev structure*/
  cdev_init(&atd_cdev,&fops);

  /* Adding character device to the system*/
  if((cdev_add(&atd_cdev,dev,1)) < 0){
    pr_err("Cannot add the device to the system\n");
    goto r_del;
  }

  /*Creating struct class*/
  if((dev_class = class_create(THIS_MODULE,"atd_class")) == NULL){
    pr_err("Cannot create the struct class\n");
    goto r_class;
  }

  /*Creating device*/
  if((device_create(dev_class,NULL,dev,NULL,"atd_device")) == NULL){
    pr_err( "Cannot create the Device \n");
    goto r_device;
  }
  
  //Output GPIO configuration
  //Checking the GPIO is valid or not
  if(gpio_is_valid(GPIO_21_OUT) == false){
    pr_err("GPIO %d is not valid\n", GPIO_21_OUT);
    goto r_device;
  }
  
  //Requesting the GPIO
  if(gpio_request(GPIO_21_OUT,"GPIO_21_OUT") < 0){
    pr_err("ERROR: GPIO %d request\n", GPIO_21_OUT);
    goto r_gpio_out;
  }
  
  //configure the GPIO as output
  gpio_direction_output(GPIO_21_OUT, 0);
  
  //Input GPIO configuratioin
  //Checking the GPIO is valid or not
  if(gpio_is_valid(GPIO_16_IN) == false){
    pr_err("GPIO %d is not valid\n", GPIO_16_IN);
    goto r_gpio_in;
  }
  
  //Requesting the GPIO
  if(gpio_request(GPIO_16_IN,"GPIO_16_IN") < 0){
    pr_err("ERROR: GPIO %d request\n", GPIO_16_IN);
    goto r_gpio_in;
  }
  
  //configure the GPIO as input
  gpio_direction_input(GPIO_16_IN);
  
  /*
  ** I have commented the below few lines, as gpio_set_debounce is not supported 
  ** in the Raspberry pi. So we are using EN_DEBOUNCE to handle this in this driver.
  */ 
#ifndef EN_DEBOUNCE
  //Debounce the button with a delay of 200ms
  if(gpio_set_debounce(GPIO_16_IN, 200) < 0){
    pr_err("ERROR: gpio_set_debounce - %d\n", GPIO_16_IN);
    //goto r_gpio_in;
  }
#endif
  
  //Get the IRQ number for our GPIO
  GPIO_irqNumber = gpio_to_irq(GPIO_16_IN);
  pr_info("GPIO_irqNumber = %d\n", GPIO_irqNumber);
  
  if (request_irq(GPIO_irqNumber,             //IRQ number
                  (void *)gpio_irq_handler,   //IRQ handler
                  IRQF_TRIGGER_RISING,        //Handler will be called in raising edge
                  "atd_device",               //used to identify the device name using this IRQ
                  NULL)) {                    //device id for shared IRQ
    pr_err("my_device: cannot register IRQ ");
    goto r_gpio_in;
  }
  
  
 
  pr_info("Device Driver Insert...Done!!!\n");
  return 0;

r_gpio_in:
  gpio_free(GPIO_16_IN);
r_gpio_out:
  gpio_free(GPIO_21_OUT);
r_device:
  device_destroy(dev_class,dev);
r_class:
  class_destroy(dev_class);
r_del:
  cdev_del(&atd_cdev);
r_unreg:
  unregister_chrdev_region(dev,1);
  
  return -1;
}

/*
** Module exit function
*/
static void __exit atd_driver_exit(void)
{
  free_irq(GPIO_irqNumber,NULL);
  gpio_free(GPIO_16_IN);
  gpio_free(GPIO_21_OUT);
  device_destroy(dev_class,dev);
  class_destroy(dev_class);
  cdev_del(&atd_cdev);
  unregister_chrdev_region(dev, 1);
  pr_info("Device Driver Remove...Done!!\n");
}
 
module_init(atd_driver_init);
module_exit(atd_driver_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Anit-Theft-Driver <patel.4@iitj.ac.in|anand.2@iitj.ac.in|ranamujan.1@iitj.ac.in>");
MODULE_DESCRIPTION("A simple IR driver to detect Theft");
MODULE_VERSION("1.00");