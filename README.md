# CSC591-Robotics

System Architecture
The project abandons the traditional "on-board" processing paradigm in favor of a distributed microservices approach.
​The Edge Nodes (Robots): Multiple autonomous agents simulated in Gazebo/ROS 2. These nodes will publish high-frequency RGB/Depth camera streams.
​The Networking Layer: A custom C++ middleware bridge utilizing gRPC or ZeroMQ. This layer handles asynchronous, high-throughput data serialization and routes requests from the fleet to the backend without blocking the robots' local control loops.
​The Inference Server: A centralized, multi-threaded C++ backend utilizing a framework like ONNX Runtime or TensorRT. It queues incoming image tensors, executes an object detection model (e.g., YOLOv8), and calculates the spatial coordinates of dynamic obstacles.
​The Action Loop: The server transmits targeted velocity overrides or localized waypoint adjustments back to the specific robot to avoid the detected obstacles, closing the perception-action loop.
​Alignment with Course Syllabus
​Robotic Software Architecture: Designing a highly concurrent, low-latency distributed system using C++ and modern RPC frameworks.
​Deep Learning & AI: Deploying and managing the memory lifecycle of a neural network in a C++ production environment.
​Multi-Robot Systems: Scaling the architecture to handle concurrent data streams from 3+ independent agents without system degradation.
​Testing: Implementing rigorous benchmarking and unit testing (e.g., Google Test) to validate latency limits, memory safety, and thread synchronization.
​Proposed Division of Labor (3-Person Team)
To ensure parallel development and avoid integration bottlenecks, the workload is modularized:
​Module 1: Core AI & Memory Management (C++): Responsible for the backend inference engine. Focuses on loading the deep learning model, optimizing matrix operations, and ensuring zero-copy memory management within the inference loop.
​Module 2: Distributed Systems & Networking (C++): Responsible for the gRPC/ZeroMQ communication layer. Focuses on thread pooling, message serialization, and handling asynchronous requests between the robots and the server.
​Module 3: Robotics, Simulation, & Action Generation (ROS 2 / Python): Responsible for the Gazebo environment, fleet orchestration, and translating the server's bounding-box outputs into actionable geometry_msgs/Twist commands for obstacle avoidance.