in the Reliable Ordered Messages article, it said that 

                "In my experience it’s not necessary to send perfect acks. Building a reliability system on top of a system 
                that very rarely drops acks adds no significant problems. But it does create a challenge for testing this 
                system works under all situations because of the edge cases when acks are dropped.

                So please if you do implement this system yourself, setup a soak test with terrible network conditions to 
                make sure your ack system is working correctly. You’ll find such a soak test in the example source code 
                for this article, and the open source network libraries reliable.io and yojimbo which also implement this technique."

https://gafferongames.com/post/reliable_ordered_messages/