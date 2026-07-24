#!/bin/bash
# Build script that uses Java 17 if available

# Check if Java 17 is installed
if /usr/libexec/java_home -v 17 &> /dev/null; then
    export JAVA_HOME=$(/usr/libexec/java_home -v 17)
    echo "Using Java 17: $JAVA_HOME"
    ./gradlew clean assembleDebug
else
    echo "Java 17 not found. Please install it with:"
    echo "brew install openjdk@17"
    echo ""
    echo "Then link it with:"
    echo "sudo ln -sfn /opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk /Library/Java/JavaVirtualMachines/openjdk-17.jdk"
    exit 1
fi
