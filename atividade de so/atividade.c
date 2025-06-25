#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched/signal.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define LOG_INTERVAL_MS 300000  // 5 minutos
#define PROC_FILENAME "process_log"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Seu Nome");
MODULE_DESCRIPTION("Módulo do Kernel para log de escalonamento de processos");

static struct delayed_work log_work;
static struct proc_dir_entry *proc_file;

/* Estrutura para armazenar processo temporariamente para ordenação */
struct proc_info {
    struct task_struct *task;
    int priority;
    struct list_head list;
};

static LIST_HEAD(proc_list);

/* Função para coletar e ordenar os processos */
static void collect_processes(void)
{
    struct task_struct *task;
    struct proc_info *pinfo, *tmp;

    // Limpa lista anterior
    list_for_each_entry_safe(pinfo, tmp, &proc_list, list) {
        list_del(&pinfo->list);
        kfree(pinfo);
    }

    // Coleta e armazena processos com prioridade
    for_each_process(task) {
        pinfo = kmalloc(sizeof(*pinfo), GFP_KERNEL);
        if (!pinfo)
            continue;

        pinfo->task = task;
        pinfo->priority = task->prio;
        INIT_LIST_HEAD(&pinfo->list);

        // Inserção ordenada
        struct list_head *pos;
        list_for_each(pos, &proc_list) {
            struct proc_info *entry = list_entry(pos, struct proc_info, list);
            if (pinfo->priority < entry->priority)
                break;
        }
        list_add_tail(&pinfo->list, pos);
    }
}

/* Função executada periodicamente */
static void log_processes(struct work_struct *work)
{
    struct proc_info *pinfo;

    collect_processes();

    printk(KERN_INFO "[scheduler_log] Lista de processos ordenada por prioridade:\n");
    list_for_each_entry(pinfo, &proc_list, list) {
        printk(KERN_INFO "PID: %d | Nome: %s | Prioridade: %d\n",
               pinfo->task->pid, pinfo->task->comm, pinfo->priority);
    }

    schedule_delayed_work(&log_work, msecs_to_jiffies(LOG_INTERVAL_MS));
}

/* Interface /proc para leitura */
static int proc_show(struct seq_file *m, void *v)
{
    struct proc_info *pinfo;

    list_for_each_entry(pinfo, &proc_list, list) {
        seq_printf(m, "PID: %d\tNome: %s\tPrioridade: %d\n",
                   pinfo->task->pid, pinfo->task->comm, pinfo->priority);
    }
    return 0;
}

static int proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, proc_show, NULL);
}

static const struct proc_ops proc_file_ops = {
    .proc_open    = proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* Função de inicialização */
static int __init scheduler_log_init(void)
{
    proc_file = proc_create(PROC_FILENAME, 0, NULL, &proc_file_ops);
    if (!proc_file) {
        pr_err("Não foi possível criar o arquivo /proc/%s\n", PROC_FILENAME);
        return -ENOMEM;
    }

    INIT_DELAYED_WORK(&log_work, log_processes);
    schedule_delayed_work(&log_work, msecs_to_jiffies(LOG_INTERVAL_MS));

    pr_info("scheduler_log carregado com sucesso.\n");
    return 0;
}

/* Função de limpeza */
static void __exit scheduler_log_exit(void)
{
    struct proc_info *pinfo, *tmp;

    cancel_delayed_work_sync(&log_work);

    // Limpa lista
    list_for_each_entry_safe(pinfo, tmp, &proc_list, list) {
        list_del(&pinfo->list);
        kfree(pinfo);
    }

    if (proc_file)
        proc_remove(proc_file);

    pr_info("scheduler_log descarregado com sucesso.\n");
}

module_init(scheduler_log_init);
module_exit(scheduler_log_exit);
