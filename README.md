# Energy Management System for Residential Solar and Agricultural Settings

## Overview

The Energy Management System (EMS) is a comprehensive, production-ready software solution designed for residential properties with solar photovoltaic (PV) systems, battery storage, agricultural irrigation, and electric vehicle (EV) charging capabilities. This system provides real-time control and optimization of energy resources without relying on forecasting or pricing data, focusing instead on operational efficiency, outage resilience, and resource optimization.

The system is engineered for reliability, safety, and maintainability, implementing industrial-grade practices for mission-critical energy infrastructure. It provides autonomous operation while offering extensive monitoring and control capabilities through a modern web interface.

## System Architecture

### Core Design Principles

1. **Real-time Optimization**: Continuous adjustment of energy flow based on current conditions
2. **Outage Resilience**: Automatic transition to island mode during grid outages with prioritized load management
3. **Resource Efficiency**: Optimal utilization of solar generation, battery storage, and agricultural resources
4. **Safety First**: Multi-layered safety mechanisms with fail-safe operation
5. **Modular Design**: Independent, testable components with clean interfaces
6. **Production Readiness**: Comprehensive logging, monitoring, and maintenance capabilities

### High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Web Interface & API                       │
├─────────────────────────────────────────────────────────────┤
│                 System Controller & Optimizer                │
├──────┬──────┬───────┬────────┬─────────┬─────────┬──────────┤
│  PV  │Battery│ Loads │Agriculture│   EV   │ Config │  Logger  │
│Module│Module │Module │  Module  │ Module │ Module │  Module  │
└──────┴──────┴───────┴────────┴─────────┴─────────┴──────────┘
```

### Component Interactions

1. **Measurement Collection**: All subsystems report real-time measurements at configurable intervals
2. **State Assessment**: System evaluates grid status, battery state of charge (SOC), load conditions, and resource availability
3. **Optimization Engine**: Determines optimal energy flow based on current conditions and configured priorities
4. **Control Execution**: Implements decisions through hardware interfaces with safety checks
5. **Monitoring & Logging**: Continuous system health monitoring and comprehensive data logging

## Module Descriptions

### Core Data Structures (`home_energy_common.h`)

Provides all fundamental data types, constants, and structures used throughout the system:

- **System States**: Operating modes, battery SOC categories, load priorities, irrigation modes
- **Measurement Structures**: Real-time data collection from all subsystems
- **Configuration Structures**: System parameters and operational limits
- **Control Commands**: Command structures for subsystem control
- **Error Handling**: Comprehensive error and alarm definitions

### Configuration Management (`config.c/h`)

Handles system configuration through JSON files and command-line arguments:

- **JSON Configuration**: Human-readable configuration files with validation
- **CLI Interface**: Command-line parameter override for all settings
- **Configuration Validation**: Range checking and consistency validation
- **Default Configuration**: Safe defaults for all parameters
- **Runtime Reconfiguration**: Hot-reload capability for non-critical parameters

### PV Coordination (`pv.c/h`)

Manages solar photovoltaic systems for maximum production and safety:

- **Maximum Power Point Tracking (MPPT)**: Implements multiple MPPT algorithms (Perturb & Observe, Incremental Conductance, Constant Voltage)
- **String Management**: Individual monitoring and control of PV strings
- **Fault Detection**: Open circuit, short circuit, ground fault, and string imbalance detection
- **Production Optimization**: String-level optimization and derating calculations
- **Curtailment Control**: Safe power reduction during battery saturation

### Battery Management (`battery.c/h`)

Controls battery storage systems with focus on longevity and safety:

- **State of Charge (SOC) Estimation**: Combines coulomb counting and voltage-based methods
- **Thermal Management**: Temperature monitoring and active cooling/heating control
- **Charge Algorithms**: Multi-stage charging (bulk, absorption, float, equalize)
- **Health Monitoring**: Cycle counting, capacity degradation tracking
- **Safety Systems**: Over/under voltage, overcurrent, and overtemperature protection

### Load Management (`loads.c/h`)

Manages electrical loads based on priority and available resources:

- **Priority-Based Shedding**: Five priority levels from critical to non-essential
- **Load Scheduling**: Time-based and resource-based load control
- **Timing Constraints**: Minimum on/off times to prevent short cycling
- **Load Rotation**: Fair shedding distribution for extended outages
- **Deferrable Loads**: Intelligent scheduling of non-time-critical loads

### Agriculture and Irrigation (`agriculture.c/h`)

Controls agricultural systems with water and energy optimization:

- **Soil Moisture Management**: Sensor-based irrigation control
- **Zone Management**: Independent control of irrigation zones
- **Water Optimization**: Efficient water usage with daily limits
- **Power-Aware Operation**: Irrigation scheduling based on available energy
- **Fault Detection**: Pump, valve, and sensor fault monitoring

### EV Charging (`ev.c/h`)

Manages electric vehicle charging with smart scheduling:

- **Renewable-First Charging**: Prioritizes solar energy for EV charging
- **Departure-Based Scheduling**: Completes charging by scheduled departure time
- **Rate Limiting**: Dynamic charge rate adjustment based on available power
- **Multi-EV Support**: Simultaneous management of multiple vehicles
- **Grid Interaction**: Configurable grid charging limits and preferences

### System Controller (`controller.c/h`)

The central optimization and control logic:

- **Mode Management**: Automatic transitions between operating modes
- **Energy Flow Optimization**: Real-time balancing of generation, storage, and consumption
- **Fault Handling**: System-wide fault detection and response
- **Statistics Collection**: Comprehensive energy and performance tracking
- **Safety Monitoring**: Continuous safety limit checking

### Web Server (`webserver.c/h`, `api_handler.c`)

Provides modern web interface and API:

- **RESTful API**: Comprehensive JSON API for all system functions
- **Real-time Updates**: WebSocket-based live data streaming
- **Authentication & Authorization**: Role-based access control
- **Static File Serving**: Modern web dashboard with responsive design
- **Security Features**: HTTPS support, rate limiting, input validation

## Installation and Setup

### Prerequisites

#### Hardware Requirements
- **Processor**: x86_64 or ARMv7+ with FPU
- **Memory**: Minimum 512MB RAM, 1GB recommended
- **Storage**: 100MB available space
- **Networking**: Ethernet or WiFi with static or DHCP addressing
- **Hardware Interfaces**: Supported communication buses (Modbus, CAN, RS-485) for device integration

#### Software Requirements
- **Operating System**: Linux (Ubuntu 20.04+, Debian 11+, or equivalent)
- **Compiler**: GCC 9.0+ or Clang 10.0+ with C11 support
- **Libraries**: OpenSSL 1.1.1+, Jansson 2.13+, POSIX threads
- **Build Tools**: Make, CMake (optional)

### Building from Source

#### Initial Setup
```bash
# Clone the repository
git clone https://github.com/your-organization/energy-management-system
cd energy-management-system

# Install build dependencies (Ubuntu/Debian)
sudo apt-get update
sudo apt-get install build-essential libssl-dev libjansson-dev

# Build the system
make release

# Build with debug symbols and sanitizers
make debug

# Build production version (stripped, optimized)
make production
```

#### Static Analysis
```bash
# Run comprehensive static analysis
make analyze

# Security-focused analysis
make flawfinder

# Memory safety analysis
make infer
```

### Installation

#### System Installation
```bash
# Install to system directories
sudo make install

# Install development headers and libraries
sudo make install-dev

# Verify installation
which energy_manager
energy_manager --version
```

#### Systemd Service Setup
```bash
# Copy service file
sudo cp systemd/energy_manager.service /etc/systemd/system/

# Enable and start service
sudo systemctl daemon-reload
sudo systemctl enable energy_manager
sudo systemctl start energy_manager

# Check service status
sudo systemctl status energy_manager
```

### Configuration

#### Initial Configuration
```bash
# Generate default configuration
energy_manager --config /etc/energy_manager/config.json --test

# Edit configuration
sudo nano /etc/energy_manager/config.json
```

#### Configuration Structure
```json
{
    "system_name": "Residential Energy System",
    "nominal_voltage": 240.0,
    "max_grid_import": 10000.0,
    "max_grid_export": 5000.0,
    
    "battery_settings": {
        "soc_min": 20.0,
        "soc_max": 95.0,
        "temp_max": 45.0,
        "reserve_soc": 30.0
    },
    
    "pv_settings": {
        "curtail_start": 90.0,
        "curtail_max": 50.0
    },
    
    "loads": [
        {
            "id": "refrigerator",
            "rated_power": 150.0,
            "priority": 0,
            "is_deferrable": false,
            "is_sheddable": false,
            "min_on_time": 300.0,
            "min_off_time": 600.0
        }
    ],
    
    "irrigation_zones": [
        {
            "zone_id": "vegetable_garden",
            "area_sqft": 500.0,
            "water_flow_rate": 5.0,
            "power_consumption": 750.0,
            "moisture_threshold": 30.0,
            "watering_duration": 30.0,
            "enabled": true
        }
    ],
    
    "control_parameters": {
        "control_interval": 1.0,
        "measurement_interval": 0.5,
        "hysteresis": 2.0
    }
}
```

#### Environment Variables
```bash
# Web server configuration
export WEB_PORT=8080
export WEB_SSL_PORT=8443
export WEB_STATIC_DIR=/opt/energy_manager/static
export WEB_ADMIN_PASSWORD=secure_password_hash

# System configuration
export EMS_CONFIG_FILE=/etc/energy_manager/config.json
export EMS_LOG_LEVEL=INFO
export EMS_DATA_DIR=/var/lib/energy_manager
```

## Operational Modes

### Normal Mode (Grid-Connected)
- **Description**: Standard operation with grid connection
- **Optimization**: Maximizes self-consumption of solar energy
- **Grid Interaction**: Limited import/export based on configuration
- **Load Management**: Full load availability with intelligent scheduling
- **Battery Operation**: Time-shifting of solar energy, grid charging limited

### Island Mode (Grid Outage)
- **Description**: Autonomous operation during grid outages
- **Power Source**: Solar and battery only
- **Load Management**: Priority-based load shedding activated
- **Battery Reserve**: Maintains minimum SOC for extended outages
- **PV Curtailment**: Active to prevent battery overcharging

### Critical Mode (Low Resources)
- **Description**: Minimal operation during resource-constrained conditions
- **Load Management**: Critical loads only
- **Battery Conservation**: Maximizes uptime for essential systems
- **PV Utilization**: Direct powering of critical loads when available
- **Irrigation**: Suspended unless manual override

### Maintenance Mode
- **Description**: Manual control for testing and maintenance
- **Control**: All subsystems under manual control
- **Safety**: All safety systems remain active
- **Logging**: Enhanced diagnostic logging
- **Testing**: System verification and calibration

### Emergency Mode
- **Description**: Automatic safety shutdown
- **Trigger**: Safety limit violations or critical faults
- **Actions**: Gradual shutdown of non-essential systems
- **Safety**: Maximum safety margin for all parameters
- **Recovery**: Manual intervention required for restart

## Safety Systems

### Multi-Layer Safety Architecture

#### Layer 1: Hardware Protection
- Independent hardware protection circuits
- Physical disconnects for critical faults
- Temperature fuses and circuit breakers

#### Layer 2: Software Limits
- Configurable operating limits for all parameters
- Progressive derating near limits
- Automatic shutdown on limit violations

#### Layer 3: Fault Detection
- Continuous monitoring of all subsystems
- Early warning system for degrading conditions
- Predictive fault detection where possible

#### Layer 4: Emergency Procedures
- Graceful degradation during faults
- Prioritized shutdown sequences
- Fail-safe default states

### Specific Safety Mechanisms

#### Battery Safety
- **Temperature Monitoring**: Continuous cell temperature monitoring
- **Voltage Balancing**: Active cell balancing during charging
- **Current Limiting**: Dynamic current limits based on temperature and SOC
- **Isolation Monitoring**: Ground fault detection

#### Electrical Safety
- **Arc Fault Detection**: High-frequency noise analysis
- **Ground Fault Detection**: Current imbalance monitoring
- **Overload Protection**: Progressive load shedding
- **Voltage Monitoring**: Grid and inverter voltage stability

#### Fire Safety
- **Thermal Runaway Prevention**: Multi-parameter thermal monitoring
- **Smoke Detection Integration**: External smoke detector interface
- **Automatic Shutdown**: Fire condition response protocols

## Monitoring and Diagnostics

### Real-Time Monitoring
- **Energy Flow**: Live visualization of power generation, consumption, and storage
- **System Health**: Comprehensive status of all subsystems
- **Alarm Dashboard**: Centralized alarm and warning display
- **Performance Metrics**: Efficiency calculations and performance indicators

### Data Logging
- **High-Resolution Data**: Configurable logging intervals (1s to 1hr)
- **Event Logging**: All state changes, commands, and alarms
- **Statistical Logging**: Daily, monthly, and lifetime statistics
- **Diagnostic Logging**: Detailed debug information during faults

### Diagnostic Tools
- **System Self-Test**: Comprehensive startup self-test routine
- **Component Testing**: Individual subsystem testing capabilities
- **Calibration Utilities**: Sensor calibration and verification
- **Performance Analysis**: Efficiency and performance trending

## API Documentation

### REST API Endpoints

#### System Control
```
GET    /api/system/status       # Current system status
GET    /api/system/config       # Current configuration
POST   /api/system/config       # Update configuration
GET    /api/system/stats        # System statistics
POST   /api/system/mode         # Change operating mode
```

#### Subsystem Monitoring
```
GET    /api/pv/status           # PV system status
GET    /api/battery/status      # Battery system status
GET    /api/loads/status        # Load management status
GET    /api/agriculture/status  # Irrigation system status
GET    /api/ev/status           # EV charging status
```

#### Control Endpoints
```
POST   /api/loads/control       # Load control commands
POST   /api/agriculture/control # Irrigation control
POST   /api/ev/control          # EV charging control
POST   /api/alarms/acknowledge  # Acknowledge alarms
```

#### Data Management
```
GET    /api/history             # Historical data query
GET    /api/export              # Data export
POST   /api/import              # Configuration import
```

#### Authentication
```
POST   /api/login               # User authentication
POST   /api/logout              # Session termination
GET    /api/user                # Current user info
POST   /api/apikeys             # API key management
```

### WebSocket API
- **Connection**: `ws://host:port/ws`
- **Authentication**: Token-based authentication
- **Subscriptions**: Configurable data subscriptions
- **Real-time Updates**: Push notifications for all changes

### API Security
- **Authentication**: Session-based and API key authentication
- **Authorization**: Role-based access control (viewer, operator, admin, superuser)
- **Rate Limiting**: Configurable request rate limits
- **Input Validation**: Comprehensive input sanitization and validation
- **Transport Security**: HTTPS with TLS 1.2+ required for production

## Performance Characteristics

### Control Loop Performance
- **Control Interval**: 1.0 second (configurable)
- **Measurement Interval**: 0.5 seconds (configurable)
- **Response Time**: <2 seconds for mode transitions
- **Optimization Cycle**: Continuous real-time optimization

### Resource Requirements
- **CPU Utilization**: <5% on modern hardware
- **Memory Usage**: <50MB typical, <100MB peak
- **Storage Requirements**: 
  - Application: 10MB
  - Configuration: 1MB
  - Logs: 100MB/month (configurable)
  - Data: 1GB/year (configurable)

### Network Performance
- **API Response Time**: <100ms for simple requests
- **WebSocket Latency**: <50ms for data updates
- **Concurrent Connections**: 100+ simultaneous clients
- **Data Throughput**: <10Mbps at maximum logging rate

## Testing and Validation

### Unit Testing
- **Coverage Target**: >80% code coverage
- **Test Framework**: Custom testing framework with mocking support
- **Component Isolation**: All modules independently testable
- **Continuous Integration**: Automated test execution on changes

### Integration Testing
- **Hardware-in-the-Loop**: Simulated hardware interfaces
- **System Integration**: End-to-end testing of complete system
- **Performance Testing**: Load and stress testing
- **Regression Testing**: Comprehensive test suite for all features

### Safety Validation
- **Fault Injection Testing**: Deliberate fault introduction and recovery testing
- **Boundary Testing**: Operation at all specified limits
- **Duration Testing**: Extended operation under varying conditions
- **Environmental Testing**: Temperature, humidity, and EMI testing

### Compliance Testing
- **Electrical Standards**: IEC 60364, NEC 2020 compliance where applicable
- **Safety Standards**: ISO 13849, IEC 61508 concepts applied
- **Cybersecurity**: NIST SP 800-82 guidance implementation
- **Data Privacy**: GDPR principles for data handling

## Maintenance Procedures

### Routine Maintenance
- **Log Review**: Daily review of system logs for anomalies
- **System Health Check**: Weekly automated health assessment
- **Backup Verification**: Monthly verification of configuration backups
- **Sensor Calibration**: Quarterly calibration of critical sensors

### Software Updates
- **Patch Management**: Security patch application within 72 hours of release
- **Version Updates**: Scheduled quarterly updates with thorough testing
- **Rollback Procedure**: Automated rollback to previous version on failure
- **Update Verification**: Post-update system verification and testing

### Hardware Maintenance
- **Preventive Maintenance**: Scheduled inspection and cleaning
- **Predictive Maintenance**: Wear monitoring and replacement scheduling
- **Spare Parts Inventory**: Critical spare parts maintained on-site
- **Vendor Coordination**: Manufacturer support coordination for complex issues

### Disaster Recovery
- **Backup Strategy**: Automated daily backups with off-site storage
- **Recovery Procedures**: Documented recovery procedures for all failure modes
- **Business Continuity**: Minimum functionality definition during recovery
- **Testing**: Quarterly disaster recovery testing

## Troubleshooting Guide

### Common Issues and Solutions

#### System Won't Start
1. **Check logs**: `journalctl -u energy_manager`
2. **Verify configuration**: `energy_manager --config /path/to/config.json --test`
3. **Check dependencies**: Verify all required libraries are installed
4. **Verify permissions**: Ensure proper file and directory permissions

#### Communication Failures
1. **Check hardware connections**: Verify all communication cables
2. **Test communication interfaces**: Use diagnostic tools to test Modbus/CAN/RS-485
3. **Review configuration**: Verify device addresses and communication parameters
4. **Check for interference**: Verify proper grounding and shielding

#### Performance Issues
1. **Check system resources**: Monitor CPU, memory, and disk usage
2. **Review logging configuration**: Reduce log level if excessive logging
3. **Optimize control intervals**: Adjust measurement and control intervals
4. **Check for hardware issues**: Verify sensor and controller performance

#### Safety System Activation
1. **Review alarm logs**: Identify triggering conditions
2. **Check sensor readings**: Verify sensor accuracy and calibration
3. **Review configuration**: Verify safety limits are properly configured
4. **Contact support**: For persistent safety system activations

### Diagnostic Commands
```bash
# System status
systemctl status energy_manager

# Real-time logs
journalctl -u energy_manager -f

# Configuration test
energy_manager --config /etc/energy_manager/config.json --test

# Memory usage
ps aux | grep energy_manager

# Network connections
ss -tulpn | grep energy_manager

# Disk usage
du -sh /var/log/energy_manager /var/lib/energy_manager
```

## Roadmap and Future Development

### Phase 1: Core System Enhancement (Current - Q4 2024)
- **Hardware Abstraction Layer**: Standardized interfaces for hardware manufacturers
- **Modbus Integration**: Complete support for Modbus RTU/TCP communication
- **CAN Bus Support**: Native CAN bus communication for battery systems
- **Database Integration**: SQLite/PostgreSQL for historical data storage
- **Web Interface Enhancement**: Advanced visualization and control features

### Phase 2: Advanced Features (Q1-Q2 2025)
- **Predictive Control**: Weather prediction integration for proactive management
- **Machine Learning**: Load prediction and optimization algorithms
- **Multi-Home Coordination**: Coordination between multiple energy systems
- **Demand Response**: Utility program participation capabilities
- **Advanced Irrigation**: Crop-specific watering algorithms and fertigation control

### Phase 3: Ecosystem Integration (Q3-Q4 2025)
- **Smart Home Integration**: Home Assistant, OpenHAB, and other automation systems
- **Utility APIs**: Direct integration with utility company systems
- **Vehicle-to-Grid (V2G)**: Bidirectional EV charging support
- **Microgrid Coordination**: Peer-to-peer energy sharing capabilities
- **Blockchain Integration**: Energy trading and verification systems

### Phase 4: Specialized Features (2026)
- **Hydrogen System Support**: Electrolyzer and fuel cell integration
- **Agricultural Expansion**: Livestock monitoring, automated feeding systems
- **Water Management**: Rainwater harvesting, greywater recycling integration
- **Carbon Tracking**: Emissions monitoring and reduction reporting
- **Disaster Mode**: Enhanced resilience for extreme weather events

### Technical Debt Reduction
- **Unit Test Coverage**: Increase to >90% across all modules
- **Integration Testing**: Complete hardware-in-the-loop test framework
- **Performance Optimization**: Reduce control loop latency to <500ms
- **Code Documentation**: Complete API documentation with examples
- **Security Audit**: Annual comprehensive security review and hardening

## Contributing Guidelines

### Development Process
1. **Fork Repository**: Create personal fork of the main repository
2. **Feature Branches**: Develop features in isolated branches
3. **Code Review**: All changes require peer review before merging
4. **Testing**: Complete test suite execution before submission
5. **Documentation**: Update relevant documentation with changes

### Code Standards
- **Coding Style**: Follow project-specific style guide (based on Linux kernel style)
- **Documentation**: Comprehensive comments for all public interfaces
- **Testing**: Unit tests for all new functionality
- **Security**: Security review for all external interfaces
- **Performance**: Performance testing for critical paths

### Submission Process
1. **Create Issue**: Document feature or bug fix requirement
2. **Develop Solution**: Implement in feature branch
3. **Run Tests**: Execute complete test suite
4. **Submit Pull Request**: Include detailed description and testing results
5. **Address Feedback**: Incorporate review feedback
6. **Merge**: Upon approval and successful CI execution

### Quality Requirements
- **Test Coverage**: Minimum 80% coverage for new code
- **Static Analysis**: Zero warnings from static analysis tools
- **Performance**: No regression in performance benchmarks
- **Security**: No critical or high severity security issues
- **Documentation**: Complete API and user documentation

## License and Legal

### License
This project is licensed under the GNU General Public License v3.0 (GPL-3.0). See the LICENSE file for complete terms.

### Third-Party Components
- **OpenSSL**: Apache 2.0 License
- **Jansson**: MIT License
- **Chart.js**: MIT License
- **Moment.js**: MIT License

### Compliance Notices
- **Export Control**: May be subject to export control regulations
- **Safety Standards**: Not a certified safety system without additional validation
- **Warranty**: Provided "as-is" without warranty of any kind
- **Liability**: Limited liability as specified in license terms

### Intellectual Property
- **Patents**: No known patent restrictions
- **Trademarks**: All trademarks belong to their respective owners
- **Attribution**: Required attribution for derived works

## Support and Resources

### Documentation
- **API Documentation**: Complete REST API documentation available at `/api-docs`
- **Configuration Guide**: Detailed configuration reference in `docs/configuration.md`
- **Installation Guide**: Step-by-step installation instructions in `docs/installation.md`
- **Troubleshooting Guide**: Common issues and solutions in `docs/troubleshooting.md`

### Community Support
- **GitHub Issues**: Bug reports and feature requests
- **Discussion Forum**: Community support and knowledge sharing
- **Wiki**: Community-maintained documentation and examples
- **Mailing List**: Announcements and technical discussions

### Professional Support
- **Commercial Support**: Available for enterprise deployments
- **Consulting Services**: System design and integration consulting
- **Training Programs**: Operator and administrator training
- **Custom Development**: Feature development and customization

### Security Reporting
- **Responsible Disclosure**: Report security issues to security@example.com
- **PGP Key**: Available for encrypted communication
- **Response Time**: Initial response within 72 hours for security issues
- **Acknowledgement**: Credit for security researchers upon request

## Acknowledgments

### Technical Foundations
- Design based on industry best practices for energy management systems
- Algorithms derived from academic research in renewable energy optimization
- Testing methodology inspired by safety-critical system development practices
- Architecture patterns from industrial control system design

### Standards Compliance
- Electrical safety concepts from IEC 60364 and NEC 2020
- Software engineering practices from ISO/IEC 12207
- Security principles from NIST SP 800-82
- Quality management from ISO 9001 concepts

### Community Contributions
- Open source community for foundational libraries and tools
- Academic research community for algorithm development
- Industry partners for real-world testing and validation
- Early adopters for feedback and improvement suggestions

### Special Thanks
- Electrical engineering consultants for safety guidance
- Agricultural experts for irrigation optimization algorithms
- Battery technology specialists for longevity optimization
- Grid integration experts for interconnection guidance

---

*Important Notice: This system is designed for integration with certified hardware components. Proper installation by qualified professionals is required for safe operation. Always comply with local electrical codes, regulations, and utility interconnection requirements. The software provider assumes no liability for improper installation, configuration, or operation.*
