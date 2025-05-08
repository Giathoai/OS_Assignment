import matplotlib.pyplot as plt

# Execution blocks (CPU, PID, start_time, end_time)
schedule = [
    (0, 1, 0, 4),
    (0, 3, 4, 8),
    (0, 3, 8, 12),
    (0, 3, 12, 15),
    (1, 2, 2, 6),
    (1, 2, 6, 10),
    (1, 2, 10, 14),
    (1, 1, 14, 18),
    (1, 1, 18, 21)
]

colors = {1: 'skyblue', 2: 'orange', 3: 'lightgreen'}
cpu_y = {0: 30, 1: 10}  # Y position of CPU lanes

fig, ax = plt.subplots(figsize=(12, 4))

# Plot the schedule
for cpu, pid, start, end in schedule:
    ax.broken_barh([(start, end - start)], (cpu_y[cpu], 8),
                   facecolors=colors[pid], edgecolor='black',
                   label=f"PID {pid}")

# Avoid duplicate labels
handles, labels = ax.get_legend_handles_labels()
unique = dict(zip(labels, handles))
ax.legend(unique.values(), unique.keys(), loc='upper right')

# Axes formatting
ax.set_ylim(0, 50)
ax.set_xlim(0, 22)
ax.set_xlabel("Time")
ax.set_yticks([cpu_y[0] + 4, cpu_y[1] + 4])
ax.set_yticklabels(['CPU 0', 'CPU 1'])
ax.set_title("Gantt Chart - CPU Scheduling")

plt.grid(True)
plt.tight_layout()
plt.show()