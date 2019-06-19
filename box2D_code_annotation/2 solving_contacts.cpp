
we now look at the logic flow for resolving contacts 

so if you came from article 1, we are looking at the function

                
                b2World.cpp 
                // Find islands, integrate and solve constraints, solve position constraints
                void b2World::Solve(const b2TimeStep& step);
                
so the idea is that want to group bodies into islands. and we solve the contacts for bodies that are in an island 


1.  so we first create a island. This is like a temporary variable 
    
    -   we go through all the bodyList and add it to our island variable

    -   everytime we start, we call b2island.clear(); for the new iteration.

    -   then we solve it for this island 

                b2World.cpp

                void b2World::Solve(const b2TimeStep& step)
                {
                    // Size the island for the worst case.
                    b2Island island(m_bodyCount,
                                    m_contactManager.m_contactCount,
                                    m_jointCount,
                                    &m_stackAllocator,
                                    m_contactManager.m_contactListener);

                    ...
                    ...
                    

                    for (b2Body* seed = m_bodyList; seed; seed = seed->m_next)
                    {
                        island.Clear();

                        while (stackCount > 0)
                        {
                            ...
                            
                            for (b2ContactEdge* ce = b->m_contactList; ce; ce = ce->next)
                            {
                                ...
                                island.Add(contact);
                                ...
                            }
                            
                            ...
                            ...
                            
                            for (b2JointEdge* je = b->m_jointList; je; je = je->next)
                            {
                                ...
                                island.Add(je->joint);
                                ...
                            }

                            ...
                        }

                        island.Solve(&profile, step, m_gravity, m_allowSleep);
                    }
                }



2.  now look at the island::Solve(); function

-   notice we first integrate, and we also applying damping

-   notice that we are copying position, acceleration, velocity and angular acceleration 

                b2Body* b = m_bodies[i];

                b2Vec2 c = b->m_sweep.c;
                float32 a = b->m_sweep.a;
                b2Vec2 v = b->m_linearVelocity;
                float32 w = b->m_angularVelocity;

    into the island::m_positions and island::m_velocities
    we arent directly editing m_bodies yet, as shown below:

                class b2Island
                {
                    ...
                    ...

                    b2Position* m_positions;
                    b2Velocity* m_velocities;
                };


-   notice how we are doing the integration for the velocity
                
                v += h * (b->m_gravityScale * gravity + b->m_invMass * b->m_force);

    we pass in the gravity and add it to the velocity
    notice that different from cyclone physics, we dont actually store acceleration at all.
    we do store the force acting on the body. 


-   full code below:


                b2Island.cpp

                void b2Island::Solve(b2Profile* profile, const b2TimeStep& step, const b2Vec2& gravity, bool allowSleep)
                {
                    float32 dt = step.dt;

                    // Integrate velocities and apply damping. Initialize the body state.
                    for (int32 i = 0; i < m_bodyCount; ++i)
                    {
                        b2Body* b = m_bodies[i];

                        b2Vec2 c = b->m_sweep.c;
                        float32 a = b->m_sweep.a;
                        b2Vec2 v = b->m_linearVelocity;
                        float32 w = b->m_angularVelocity;

                        // Store positions for continuous collision.
                        b->m_sweep.c0 = b->m_sweep.c;
                        b->m_sweep.a0 = b->m_sweep.a;

                        if (b->m_type == b2_dynamicBody)
                        {
                            // Integrate velocities.
                            v += dt * (b->m_gravityScale * gravity + b->m_invMass * b->m_force);
                            w += dt * b->m_invI * b->m_torque;

                            // Apply damping.
                            // ODE: dv/dt + c * v = 0
                            // Solution: v(t) = v0 * exp(-c * t)
                            // Time step: v(t + dt) = v0 * exp(-c * (t + dt)) = v0 * exp(-c * t) * exp(-c * dt) = v * exp(-c * dt)
                            // v2 = exp(-c * dt) * v1
                            // Pade approximation:
                            // v2 = v1 * 1 / (1 + c * dt)
                            v *= 1.0f / (1.0f + dt * b->m_linearDamping);
                            w *= 1.0f / (1.0f + dt * b->m_angularDamping);
                        }

                        m_positions[i].c = c;
                        m_positions[i].a = a;
                        m_velocities[i].v = v;
                        m_velocities[i].w = w;
                    }

                    ...
                    ...

                }



3.  then we want to whip out our contact Solver
    as you can see we are doing all the definitions for b2SolverData, b2ContactSolverDef and b2ContactSolver;

                void b2Island::Solve(b2Profile* profile, const b2TimeStep& step, const b2Vec2& gravity, bool allowSleep)
                {
                    ...................................................
                    ......... intergration and damping ................
                    ...................................................

                    // Solver data
                    b2SolverData solverData;
                    solverData.step = step;
                    solverData.positions = m_positions;
                    solverData.velocities = m_velocities;

                    // Initialize velocity constraints.
                    b2ContactSolverDef contactSolverDef;
                    contactSolverDef.step = step;
                    contactSolverDef.contacts = m_contacts;
                    contactSolverDef.count = m_contactCount;
                    contactSolverDef.positions = m_positions;
                    contactSolverDef.velocities = m_velocities;
                    contactSolverDef.allocator = m_allocator;

                    b2ContactSolver contactSolver(&contactSolverDef);
                    contactSolver.InitializeVelocityConstraints();

                    ...
                    ...
                }




4.  now we look at how the contactSolver is done. here we will introduce two structs :
    the b2ContactVelocityConstraint and b2ContactPositionConstraint


                b2ContactSolver.h 

                struct b2ContactVelocityConstraint
                {
                    b2VelocityConstraintPoint points[b2_maxManifoldPoints];
                    b2Vec2 normal;
                    b2Mat22 normalMass;
                    b2Mat22 K;
                    int32 indexA;
                    int32 indexB;
                    float32 invMassA, invMassB;
                    float32 invIA, invIB;
                    float32 friction;
                    float32 restitution;
                    float32 tangentSpeed;
                    int32 pointCount;
                    int32 contactIndex;
                };

and 

                b2ContactSolver.hcpp 

                struct b2ContactPositionConstraint
                {
                    b2Vec2 localPoints[b2_maxManifoldPoints];
                    b2Vec2 localNormal;
                    b2Vec2 localPoint;
                    int32 indexA;
                    int32 indexB;
                    float32 invMassA, invMassB;
                    b2Vec2 localCenterA, localCenterB;
                    float32 invIA, invIB;
                    b2Manifold::Type type;
                    float32 radiusA, radiusB;
                    int32 pointCount;
                };

as you can see, it contains core data used when resolving collision


notice in the velocity constraint, we have 


                struct b2ContactVelocityConstraint
                {
                    b2VelocityConstraintPoint points[b2_maxManifoldPoints];
                    ...
                    ...
                };

the struct for b2VelocityConstraintPoint is below:

                b2ContactSolver.h

                struct b2VelocityConstraintPoint
                {
                    b2Vec2 rA;
                    b2Vec2 rB;
                    float32 normalImpulse;
                    float32 tangentImpulse;
                    float32 normalMass;
                    float32 tangentMass;
                    float32 velocityBias;
                };
notice how it contains rA, rB, which is the relative position with BodyA, and BodyB.
these will mainly be used when doing rotation








5.  now back to our InitializeVelocityConstraints(); function.
    as you can see, we first define our b2ContactVelocityConstraint and b2ContactPositionConstraint




                b2ContactSolver.cpp

                void b2ContactSolver::InitializeVelocityConstraints()
                {
                    for (int32 i = 0; i < m_count; ++i)
                    {
                        b2ContactVelocityConstraint* vc = m_velocityConstraints + i;
                        b2ContactPositionConstraint* pc = m_positionConstraints + i;

                        ..................................................................
                        ............ Initializing b2ContactVelocityConstraint ............
                        ............ and b2ContactPositionConstraint .....................
                        ..................................................................


                    }
                }


6.  now we look at further down. 
-   notice that we find the ffective mass 

                float32 rnA = b2Cross(vcp->rA, vc->normal);
                float32 rnB = b2Cross(vcp->rB, vc->normal);

                float32 kNormal = mA + mB + iA * rnA * rnA + iB * rnB * rnB;

                vcp->normalMass = kNormal > 0.0f ? 1.0f / kNormal : 0.0f;



-   then we setup a velocityBias for restitution 
    first we compute the closing velocity along the normal 
    as you can see, we add both the linear and the angular component, and we dot product it with the normal 


                vcp->velocityBias = 0.0f;
                float32 vRel = b2Dot(vc->normal, vB + b2Cross(wB, vcp->rB) - vA - b2Cross(wA, vcp->rA));
                if (vRel < -b2_velocityThreshold)
                {
                    vcp->velocityBias = -vc->restitution * vRel;
                }





-   full code below: 

                b2ContactVelocityConstraint* vc = m_velocityConstraints + i;
                b2ContactPositionConstraint* pc = m_positionConstraints + i;

                ..................................................................
                ............ Initializing b2ContactVelocityConstraint ............
                ............ and b2ContactPositionConstraint .....................
                ..................................................................


                int32 pointCount = vc->pointCount;
                for (int32 j = 0; j < pointCount; ++j)
                {
                    b2VelocityConstraintPoint* vcp = vc->points + j;

                    vcp->rA = worldManifold.points[j] - cA;
                    vcp->rB = worldManifold.points[j] - cB;

                    float32 rnA = b2Cross(vcp->rA, vc->normal);
                    float32 rnB = b2Cross(vcp->rB, vc->normal);

                    float32 kNormal = mA + mB + iA * rnA * rnA + iB * rnB * rnB;

                    vcp->normalMass = kNormal > 0.0f ? 1.0f / kNormal : 0.0f;

                    b2Vec2 tangent = b2Cross(vc->normal, 1.0f);

                    float32 rtA = b2Cross(vcp->rA, tangent);
                    float32 rtB = b2Cross(vcp->rB, tangent);

                    float32 kTangent = mA + mB + iA * rtA * rtA + iB * rtB * rtB;

                    vcp->tangentMass = kTangent > 0.0f ? 1.0f /  kTangent : 0.0f;

                    // Setup a velocity bias for restitution.
                    vcp->velocityBias = 0.0f;
                    float32 vRel = b2Dot(vc->normal, vB + b2Cross(wB, vcp->rB) - vA - b2Cross(wA, vcp->rA));
                    if (vRel < -b2_velocityThreshold)
                    {
                        vcp->velocityBias = -vc->restitution * vRel;
                    }
                }



7.  and we look further down. notice that if you have two points, we do the block solver 
    for boxes, we would use this block solver 

    this is just solving the matrix, mentioned in his Sequential Impulse Solver video talk

                // If we have two points, then prepare the block solver.
                if (vc->pointCount == 2 && g_blockSolve)
                {
                    b2VelocityConstraintPoint* vcp1 = vc->points + 0;
                    b2VelocityConstraintPoint* vcp2 = vc->points + 1;

                    float32 rn1A = b2Cross(vcp1->rA, vc->normal);
                    float32 rn1B = b2Cross(vcp1->rB, vc->normal);
                    float32 rn2A = b2Cross(vcp2->rA, vc->normal);
                    float32 rn2B = b2Cross(vcp2->rB, vc->normal);

                    float32 k11 = mA + mB + iA * rn1A * rn1A + iB * rn1B * rn1B;
                    float32 k22 = mA + mB + iA * rn2A * rn2A + iB * rn2B * rn2B;
                    float32 k12 = mA + mB + iA * rn1A * rn2A + iB * rn1B * rn2B;

                    // Ensure a reasonable condition number.
                    const float32 k_maxConditionNumber = 1000.0f;
                    if (k11 * k11 < k_maxConditionNumber * (k11 * k22 - k12 * k12))
                    {
                        // K is safe to invert.
                        vc->K.ex.Set(k11, k12);
                        vc->K.ey.Set(k12, k22);
                        vc->normalMass = vc->K.GetInverse();
                    }
                    else
                    {
                        // The constraints are redundant, just use one.
                        // TODO_ERIN use deepest?
                        vc->pointCount = 1;
                    }
                }






8.  so now back to section3, we finished talking about contactSolver.InitializeVelocityConstraints();

    now we get to solving the velocity constraints.

    we wont look at the joints VelocityConstraints. but only the regular VelocityConstraints


                b2island.cpp

                void b2Island::Solve(b2Profile* profile, const b2TimeStep& step, const b2Vec2& gravity, bool allowSleep)
                {
                    ...................................................
                    ......... intergration and damping ................
                    ...................................................

                    // Solver data
                    b2SolverData solverData;
                    solverData.step = step;
                    solverData.positions = m_positions;
                    solverData.velocities = m_velocities;

                    // Initialize velocity constraints.
                    b2ContactSolverDef contactSolverDef;
                    contactSolverDef.step = step;
                    contactSolverDef.contacts = m_contacts;
                    contactSolverDef.count = m_contactCount;
                    contactSolverDef.positions = m_positions;
                    contactSolverDef.velocities = m_velocities;
                    contactSolverDef.allocator = m_allocator;

                    b2ContactSolver contactSolver(&contactSolverDef);
                    contactSolver.InitializeVelocityConstraints();

                    ...
                    ...


                    timer.Reset();
                    for (int32 i = 0; i < step.velocityIterations; ++i)
                    {
                        for (int32 j = 0; j < m_jointCount; ++j)
                        {
                            m_joints[j]->SolveVelocityConstraints(solverData);
                        }

                        contactSolver.SolveVelocityConstraints();
                    }
                }

