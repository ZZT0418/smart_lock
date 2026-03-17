#ifndef _FINGER_MANAGER_H_
#define _FINGER_MANAGER_H_ 

void finger_add(void);           // 录入指纹
void finger_delete_last(void);   // 删除最后一个指纹
void finger_verify(void);        // 刷指纹验证
void servo_unlock(void);         // 舵机开锁接口

#endif