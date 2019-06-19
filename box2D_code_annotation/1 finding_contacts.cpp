
we start from Tiles.h


1.  
                Tiles.h

                class Tiles : public Test
                {
                    Tiles()
                    {

                    }
                };



2.  then we look at its Step(); function


                Tiles.h

                void Step(Settings* settings)
                {
                    ...
                    Test::Step(settings);
                    ...
                }





3.  As mentioned in section one, Tiles is a child of Test class.
the Step(); function is just calling m_world Step


                Test.cpp

                void Test::Step(Settings* settings)
                {
                    ...
                    ...
                    m_world->Step(timeStep, settings->velocityIterations, settings->positionIterations);

                    ...
                }



4.  we look at the b2World.h file 
    you can see the main steps here

-   we first find all the contacts in the new frame

-   then we collide these contacts

-   then we resolve these contacts.

lets look at these one by one 


                Box2D/Dynamics/b2World.h

                void b2World::Step(float32 dt, int32 velocityIterations, int32 positionIterations)
                {
                    m_contactManager.FindNewContacts();
            
                    ...
                    ...

                    // Update contacts. This is where some contacts are destroyed.
                    {
                        b2Timer timer;
                        m_contactManager.Collide();
                        m_profile.collide = timer.GetMilliseconds();
                    }

                    // Integrate velocities, solve velocity constraints, and integrate positions.
                    if (m_stepComplete && step.dt > 0.0f)
                    {
                        b2Timer timer;
                        Solve(step);
                        m_profile.solve = timer.GetMilliseconds();
                    }

                    // Handle TOI events.
                    if (m_continuousPhysics && step.dt > 0.0f)
                    {
                        b2Timer timer;
                        SolveTOI(step);
                        m_profile.solveTOI = timer.GetMilliseconds();
                    }

                    ...
                    ...
                }



##################################################################################
###################### b2ContactManager::FindNewContacts(); ######################
##################################################################################


5.   let us look at how the FindNewContacts(); in b2ContactManager.cpp function works


                b2ContactManager.cpp

                void b2ContactManager::FindNewContacts()
                {
                    m_broadPhase.UpdatePairs(this);
                }

i will skip this part here since thats not what I am look for for now



6.  Essentially each contact gets added to this b2Contact array

                b2ContactManager.h

                class b2ContactManager
                {

                    b2BroadPhase m_broadPhase;
                    b2Contact* m_contactList;   <---------------
                    int32 m_contactCount;
                };

so we will getting the contacts of in this frame from this array



##################################################################################
########################### b2ContactManager::Collide(); #########################
##################################################################################

7.  returning to b2World.Step(); function. Once we find all of our contacts, let us look at the Collide function


                b2ContactManager.Collide()
                {
                    b2Contact* c = m_contactList;
                    while (c)
                    {
                        ...
                        ...

                        // The contact persists.
                        c->Update(m_contactListener);
                        c = c->GetNext();
                    }
                }



8.  let us take a look at the b2Contact::Update(); function 
                

                b2Contact.cpp

                void b2Contact::Update(b2ContactListener* listener)
                {

                    ...
                    ...

                    Evaluate(&m_manifold, xfA, xfB);
                
                    ............................
                    ...... warm starting .......
                    ............................

                    if (wasTouching == false && touching == true && listener)
                    {
                        listener->BeginContact(this);
                    }

                    if (wasTouching == true && touching == false && listener)
                    {
                        listener->EndContact(this);
                    }

                    if (sensor == false && touching && listener)
                    {
                        listener->PreSolve(this, &oldManifold);
                    }
                }



9.  let us take a look a the Evaluate();
just for here, we will look at the CircleCircle collision

                b2CircleContact.cpp

                void b2CircleContact::Evaluate(b2Manifold* manifold, const b2Transform& xfA, const b2Transform& xfB)
                {
                    b2CollideCircles(manifold,
                                    (b2CircleShape*)m_fixtureA->GetShape(), xfA,
                                    (b2CircleShape*)m_fixtureB->GetShape(), xfB);
                }




10. let us look at the BeginContact();, EndContact(); and PreSolve(); funtion


one thing we want to point out is the PreSolve(); function
notice in the Test class, we have 

                Test.h

                class Test : public b2ContactListener
                {
                    ...
                    ContactPoint m_points[k_maxContactPoints];
                    int32 m_pointCount;
                };  

this is mainly used for debugging 
so in the PreSolve(); function, we copy the list b2Contact information and give it to the Test.
This way Test can render it.

                Test.cpp

                void Test::PreSolve(b2Contact* contact, const b2Manifold* oldManifold)
                {
                    const b2Manifold* manifold = contact->GetManifold();

                    if (manifold->pointCount == 0)
                    {
                        return;
                    }

                    b2Fixture* fixtureA = contact->GetFixtureA();
                    b2Fixture* fixtureB = contact->GetFixtureB();

                    b2PointState state1[b2_maxManifoldPoints], state2[b2_maxManifoldPoints];
                    b2GetPointStates(state1, state2, oldManifold, manifold);

                    b2WorldManifold worldManifold;
                    contact->GetWorldManifold(&worldManifold);

                    for (int32 i = 0; i < manifold->pointCount && m_pointCount < k_maxContactPoints; ++i)
                    {
                        ContactPoint* cp = m_points + m_pointCount;
                        cp->fixtureA = fixtureA;
                        cp->fixtureB = fixtureB;
                        cp->position = worldManifold.points[i];
                        cp->normal = worldManifold.normal;
                        cp->state = state2[i];
                        cp->normalImpulse = manifold->points[i].normalImpulse;
                        cp->tangentImpulse = manifold->points[i].tangentImpulse;
                        cp->separation = worldManifold.separations[i];
                        ++m_pointCount;
                    }
                }


