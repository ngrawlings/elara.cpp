//
//  Order.cpp
//  LibElaraData
//
//  Created by Nyhl Rawlings on 02/05/2019.
//  Copyright © 2019 Liquidsoft Studio. All rights reserved.
//

#include "Order.h"

namespace nrcore {
    namespace sql {
        
        Order::Order() {
            
        }
        
        Order::Order(const Order &order) {
            orders = order.orders;
        }
        
        Order::~Order() {
            
        }
        
        void Order::add(String field, DIRECTION direction) {
            ORDER *order = new ORDER;
            order->field = field;
            order->direction = direction;
            orders.push(Ref<ORDER>(order));
        }
        
        void Order::clear() {
            orders.clear();
        }
        
        String Order::toString() {
            String ret;
            
            size_t len = orders.length();
            if (len) {
                ret = String("`%` %").arg(orders[0].getPtr()->field).arg(orders[0].getPtr()->direction == 0 ? "ASC" : "DESC");
                
                for(int i=1; i<len; i++)
                    ret += String(", `%` %").arg(orders[i].getPtr()->field).arg(orders[i].getPtr()->direction);
            }
            
            return ret;
        }
        
    }
}
